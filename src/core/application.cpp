#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "application.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
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
    sdkInstance.initSdkGlobals(proxy);
    hookManagerInstance.setSdkContext(&sdkInstance);
    initThread = std::thread(&Application::initHooksThread, this);
    return true;
}

void Application::detach() {
    Logger->debug("SPI detach - removing hooks");
    didRequestExit.store(true);
    if (initThread.joinable()) {
        initThread.join();
    }

    uninstallAllHooks();

    if (rendererInstance.isImGuiInitialized()) {
        gameWindowInstance.restoreAll();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        rendererInstance.shutdown();
    }
}

void Application::uninstallAllHooks() {
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
        return app.rendererInstance.origPresent() ? app.rendererInstance.origPresent()(pSwapChain, SyncInterval, Flags) : S_OK;
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
            SHORT ks = GetAsyncKeyState(VK_F10);
            bool currentF10 = (ks & 0x8000) != 0;
            if (currentF10 && !app.previousF10) {
                std::atomic<bool>& showUI = app.ui().showUI();
                showUI = !showUI.load();
                app.ui().applyUIInputState(app.gameWindowInstance);

                std::ostringstream ss;
                ss << "Toggle UI: visible=" << showUI.load();
                Logger->debug(ss.str());
            }
            app.previousF10 = currentF10;

            app.engine().applyHUDVisibility();

            app.rendererInstance.ensureRenderTarget(pSwapChain);
            app.rendererInstance.beginRender();

            ImGui_ImplDX11_NewFrame();
            app.mouse().cursorPassthrough() = true;
            ImGui_ImplWin32_NewFrame();
            app.mouse().cursorPassthrough() = false;

            ImGui::NewFrame();
            if (app.ui().showUI().load()) {
                app.ui().renderOverlayContents(app.rendererInstance);
            }
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    } SAS_HOOK_CATCH_VOID

    return app.rendererInstance.origPresent() ? app.rendererInstance.origPresent()(pSwapChain, SyncInterval, Flags) : S_OK;
}

HRESULT STDMETHODCALLTYPE Application::resizeBuffersDetour(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                                           UINT SwapChainFlags) {
    ZoneScopedN("ResizeBuffersDetour");
    Application& app = instance();
    if (app.didRequestExit.load()) {
        return app.rendererInstance.origResizeBuffers()
                   ? app.rendererInstance.origResizeBuffers()(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
                   : S_OK;
    }

    SAS_HOOK_TRY {
        app.rendererInstance.releaseRenderTargetView();
    } SAS_HOOK_CATCH_VOID

    return app.rendererInstance.origResizeBuffers()
               ? app.rendererInstance.origResizeBuffers()(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
               : S_OK;
}

LRESULT CALLBACK Application::wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ZoneScopedN("WndProc");
    Application& app = instance();
    SAS_HOOK_TRY {
        if (app.ui().showUI().load()) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true;
            }
            switch (uMsg) {
                case WM_MOUSEMOVE: {
                    if (app.engine().isCameraDragActive().load()) {
                        break;
                    }
                    return 0;
                }
                case WM_RBUTTONDOWN: {
                    if (app.engine().isCameraDragActive().load()) {
                        break;
                    }
                    POINT spt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ClientToScreen(hWnd, &spt);
                    RECT uiRect = app.rendererInstance.uiRect();
                    if (PtInRect(&uiRect, spt)) {
                        return 0;
                    }
                    app.engine().isCameraDragActive() = true;
                    break;
                }
                case WM_RBUTTONUP: {
                    app.engine().isCameraDragActive() = false;
                    break;
                }
                case WM_RBUTTONDBLCLK: {
                    if (app.engine().isCameraDragActive().load()) {
                        break;
                    }
                    return 0;
                }
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
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
