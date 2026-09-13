#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "application.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "implot.h"
#include <sstream>
#include <windowsx.h>

#include "tracy.h"

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Application* Application::appInstance = nullptr;

Application::Application() {
    appInstance = this;
}

Application::~Application() {
    if (appInstance == this) {
        appInstance = nullptr;
    }
}

bool Application::attach(ISharedProxyInterface* proxy) {
    Logger->debug("SPI attach");
    didRequestExit.store(false);
    settingsInstance.load();
    rendererInstance.applySettings(settingsInstance.options);
    sdkInstance.initSdkGlobals(proxy);
    hookManagerInstance.setSdkContext(&sdkInstance);
    initThread = std::thread(&Application::initHooksThread, this);
    return true;
}

void Application::detach() {
    Logger->debug("SPI detach - removing hooks");
    didRequestExit.store(true);
    if (settingsInstance.didSettingsChange()) {
        settingsInstance.save();
    }
    if (initThread.joinable()) {
        initThread.join();
    }

    uninstallAllHooks();

    if (rendererInstance.isImGuiInitialized()) {
        gameWindowInstance.restoreAll();
        photoOverlayInstance.shutdown();
        lutStackInstance.shutdown();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        rendererInstance.shutdown();
    }
}

void Application::uninstallAllHooks() {
    lutStackInstance.removeDepthHook();
    rendererInstance.removeHooks();
    if (hookManagerInstance.areHooksInstalled()) {
        hookManagerInstance.uninstallAll();
    }
}

void Application::initHooksThread() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const int maxAttempts = 8;
    for (int attempt = 0; attempt < maxAttempts && !didRequestExit.load(); ++attempt) {
        if (!UObject::GObjObjects || !GMalloc || !GWorld || !sdkInstance.processEventAddress()) {
            sdkInstance.initSdkGlobals(sdkInstance.proxy());
        }

        if (sdkInstance.initializer() && rendererInstance.installHooks(&Application::presentDetour, &Application::resizeBuffersDetour)) {
            gizmoInstance.initHooks(hookManagerInstance, sdkInstance);
            engineInstance.initTickHook(hookManagerInstance, sdkInstance);
            mouseInstance.initMouseTrap(hookManagerInstance);
            mouseInstance.initDI8MouseHook(hookManagerInstance);
            installCrashHandler();
            return;
        }

        std::ostringstream ss;
        ss << "install hook install failed on attempt " << (attempt + 1) << ", retrying...";
        Logger->debug(ss.str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    Logger->debug("initHooksThread: giving up");
}

HRESULT STDMETHODCALLTYPE Application::presentDetour(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    ZoneScopedN("PresentDetour");
    FrameMark;
    Application& app = instance();
    if (app.didRequestExit.load()) {
        auto orig = app.rendererInstance.origPresent();
        if (!orig) {
            return S_OK;
        }
        return orig(pSwapChain, SyncInterval, Flags);
    }

    SAS_HOOK_TRY {
        if (!app.rendererInstance.isImGuiInitialized()) {
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(pSwapChain->GetDesc(&desc)) && desc.OutputWindow) {
                app.gameWindowInstance.setPrimary(desc.OutputWindow);
            }
            if (app.rendererInstance.initImGuiInGame(pSwapChain)) {
                app.gameWindowInstance.clearSubclassed();
                if (!app.gameWindowInstance.subclass(app.gameWindowInstance.primary(), &Application::wndProc)) {
                    Logger->debug("presentDetour: failed to set WndProc on game window");
                }
                app.gameWindowInstance.subclassAllProcessWindows(&Application::wndProc);
            }
        }

        if (app.rendererInstance.isImGuiInitialized()) {
            const std::string& hotkeyName = app.settingsInstance.options.showOverlay;
            static std::string lastHotkeyName;
            static OverlayHotkey lastHotkey{false, false, false, VK_F10};
            if (hotkeyName != lastHotkeyName) {
                OverlayHotkey parsed{};
                if (Settings::tryParseHotkey(hotkeyName, &parsed)) {
                    lastHotkey = parsed;
                }
                lastHotkeyName = hotkeyName;
            }
            bool firing = false;
            if (!app.settings().capturingHotkey) {
                bool controlState = false;
                bool altState = false;
                bool shiftState = false;
                Settings::queryModdifiers(&controlState, &altState, &shiftState);
                const bool mainDown = (GetAsyncKeyState(lastHotkey.key) & 0x8000) != 0;
                firing = mainDown && (controlState == lastHotkey.ctrl) && (altState == lastHotkey.alt) && (shiftState == lastHotkey.shift);
            }
            const bool suppressHotkey = app.hotkeyCaptureFinished;
            app.hotkeyCaptureFinished = false;
            if (firing && !app.previousHotkey && !app.settings().capturingHotkey && !suppressHotkey) {
                std::atomic<bool>& showUI = app.ui().showUI();
                showUI = !showUI.load();
                app.ui().applyUIInputState(app.gameWindowInstance);

                std::ostringstream oss;
                oss << "Overlay hotkey pressed, toggling UI to " << (showUI.load() ? "shown" : "hidden");
                Logger->debug(oss.str());
            }
            app.previousHotkey = firing;

            app.freecam().assertFreecamCache();
            app.rendererInstance.ensureRenderTarget(pSwapChain);
            app.lutStack().apply(pSwapChain, app.rendererInstance.device(), app.rendererInstance.context(), app.rendererInstance.renderTargetView());
            app.rendererInstance.beginRender();
            app.photoOverlay().sample(pSwapChain, app.rendererInstance.device(), app.rendererInstance.context());

            ImGui_ImplDX11_NewFrame();
            app.mouse().cursorPassthrough() = true;
            ImGui_ImplWin32_NewFrame();
            app.mouse().cursorPassthrough() = false;

            ImGui::NewFrame();
            const bool wasCapturingHotkey = app.settings().capturingHotkey;
            if (app.photoOverlay().isActive()) {
                app.photoOverlay().render(app.rendererInstance.device());
            }
            if (app.ui().showUI().load()) {
                app.ui().renderOverlayContents(app.rendererInstance);
            }
            app.hotkeyCaptureFinished = wasCapturingHotkey && !app.settings().capturingHotkey;
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    } SAS_HOOK_CATCH_VOID

    auto orig = app.rendererInstance.origPresent();
    if (!orig) {
        return S_OK;
    }
    return orig(pSwapChain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE Application::resizeBuffersDetour(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                                           UINT SwapChainFlags) {
    ZoneScopedN("ResizeBuffersDetour");
    Application& app = instance();
    if (app.didRequestExit.load()) {
        auto orig = app.rendererInstance.origResizeBuffers();
        if (!orig) {
            return S_OK;
        }
        return orig(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    SAS_HOOK_TRY {
        app.rendererInstance.releaseRenderTargetView();
        app.lutStack().onResize();
    } SAS_HOOK_CATCH_VOID

    auto origResize = app.rendererInstance.origResizeBuffers();
    if (!origResize) {
        return S_OK;
    }
    return origResize(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

static int g_dragLastX = 0;
static int g_dragLastY = 0;

static void seedDragOrigin(int x, int y) {
    g_dragLastX = x;
    g_dragLastY = y;
}

LRESULT CALLBACK Application::wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ZoneScopedN("WndProc");
    Application& app = instance();
    static bool dragPassthrough = false;
    SAS_HOOK_TRY {
        if (app.ui().showUI().load()) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true;
            }
            switch (uMsg) {
                case WM_MOUSEMOVE: {
                    if (app.freecam().isCameraDragActive().load()) {
                        int dx = (int)GET_X_LPARAM(lParam);
                        int dy = (int)GET_Y_LPARAM(lParam);
                        int deltaX = dx - g_dragLastX;
                        int deltaY = dy - g_dragLastY;
                        g_dragLastX = dx;
                        g_dragLastY = dy;
                        if (deltaX != 0 || deltaY != 0) {
                            CameraDragState state = (CameraDragState)app.freecam().cameraDragState().load();
                            app.freecam().moveFreecam(deltaX, deltaY, state);
                        }
                        if (dragPassthrough) {
                            break; // native orbit: game tracks the cursor
                        }
                        return 0;
                    }
                    return 0;
                }
                case WM_RBUTTONDOWN: {
                    if (app.freecam().isCameraDragActive().load()) {
                        break;
                    }
                    POINT spt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ClientToScreen(hWnd, &spt);
                    RECT uiRect = app.rendererInstance.uiRect();
                    if (PtInRect(&uiRect, spt)) {
                        return 0;
                    }
                    // Determine camera drag state based on SHIFT/CTRL keys
                    bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    CameraDragState state = CAMERA_DRAG_ORBITING;
                    if (shiftDown && ctrlDown) {
                        state = CAMERA_DRAG_VERTICAL_PANNING;
                    } else if (shiftDown) {
                        state = CAMERA_DRAG_PANNING;
                    }
                    app.freecam().cameraDragState() = state;
                    app.freecam().isCameraDragActive() = true;
                    seedDragOrigin(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                    dragPassthrough = !app.freecam().freecamWanted().load();
                    {
                        std::ostringstream ss;
                        ss << "wndProc: RMB drag start mode=" << (int)state << (dragPassthrough ? " passthrough" : " consumed");
                        Logger->debug(ss.str());
                    }
                    if (dragPassthrough) {
                        break;
                    }
                    return 0;
                }
                case WM_RBUTTONUP: {
                    if (!app.freecam().isCameraDragActive().load()) {
                        break;
                    }
                    const bool pass = dragPassthrough;
                    dragPassthrough = false;
                    app.freecam().isCameraDragActive() = false;
                    app.freecam().cameraDragState() = CAMERA_DRAG_INACTIVE;
                    if (pass) {
                        break;
                    }
                    return 0;
                }
                case WM_LBUTTONDOWN: {
                    if (app.freecam().isCameraDragActive().load()) {
                        return 0;
                    }
                    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0) {
                        return 0;
                    }
                    POINT spt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ClientToScreen(hWnd, &spt);
                    RECT uiRect = app.rendererInstance.uiRect();
                    if (PtInRect(&uiRect, spt)) {
                        return 0;
                    }
                    app.freecam().cameraDragState() = CAMERA_DRAG_VERTICAL_PANNING;
                    app.freecam().isCameraDragActive() = true;
                    seedDragOrigin(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                    dragPassthrough = false;
                    Logger->debug("wndProc: LMB lift drag start, consumed");
                    return 0;
                }
                case WM_LBUTTONUP: {
                    if (app.freecam().isCameraDragActive().load() && (CameraDragState)app.freecam().cameraDragState().load() == CAMERA_DRAG_VERTICAL_PANNING) {
                        app.freecam().isCameraDragActive() = false;
                        app.freecam().cameraDragState() = CAMERA_DRAG_INACTIVE;
                        dragPassthrough = false;
                        Logger->debug("wndProc: LMB lift drag end, consumed");
                        return 0;
                    }
                    return 0;
                }
                case WM_RBUTTONDBLCLK: {
                    if (app.engine().isCameraDragActive().load()) {
                        break;
                    }
                    return 0;
                }
                case WM_LBUTTONDBLCLK:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN:
                case WM_XBUTTONUP:
                case WM_XBUTTONDBLCLK:
                case WM_MOUSEWHEEL:
                case WM_MOUSEHWHEEL:
                case WM_INPUT: {
                    return 0;
                }
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_CHAR: {
                    return 0;
                }
                case WM_MOUSEACTIVATE: {
                    return MA_NOACTIVATE;
                }
                case WM_SETCURSOR:
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
            }
        }
        for (const auto& e : app.gameWindowInstance.subclassedWindows()) {
            if (e.first == hWnd) {
                return CallWindowProc(e.second, hWnd, uMsg, wParam, lParam);
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    } SAS_HOOK_CATCH_RET(DefWindowProc(hWnd, uMsg, wParam, lParam))
}
