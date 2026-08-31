#ifndef SAS_APPLICATION_H
#define SAS_APPLICATION_H

#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "animation.h"
#include "bones.h"
#include "engine.h"
#include "game_window.h"
#include "gizmo.h"
#include "hook_manager.h"
#include "lights.h"
#include "logger.h"
#include "mouse.h"
#include "native_renderer.h"
#include "particles.h"
#include "props.h"
#include "sdk.h"
#include "ui.h"
#include "vfx.h"
#include <atomic>
#include <thread>

class ISharedProxyInterface;
class Application {
    public:
        Application();
        ~Application();
        static Application& instance() {
            return *appInstance;
        }

        SDKContext& sdk() {
            return sdkInstance;
        }

        HookManager& hookManager() {
            return hookManagerInstance;
        }

        GameWindow& gameWindow() {
            return gameWindowInstance;
        }

        NativeRenderer& renderer() {
            return rendererInstance;
        }

        Properties& properties() {
            return propertiesInstance;
        }

        Mouse& mouse() {
            return mouseInstance;
        }

        UI& ui() {
            return uiInstance;
        }

        Engine& engine() {
            return engineInstance;
        }

        Gizmo& gizmo() {
            return gizmoInstance;
        }

        Bones& bones() {
            return bonesInstance;
        }

        Animation& animation() {
            return animationInstance;
        }

        VFXManager& vfx() {
            return vfxInstance;
        }

        ParticleManager& particles() {
            return particleInstance;
        }
        LightManager& lights() {
            return lightsInstance;
        }
        bool attach(ISharedProxyInterface* proxy);
        void detach();

    private:
        static HRESULT STDMETHODCALLTYPE presentDetour(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT STDMETHODCALLTYPE resizeBuffersDetour(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                                             UINT SwapChainFlags);
        static LRESULT CALLBACK wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        static Application* appInstance;
        void initHooksThread();
        void uninstallAllHooks();

        std::thread initThread;
        std::atomic<bool> didRequestExit{false};

        SDKContext sdkInstance;
        HookManager hookManagerInstance;
        GameWindow gameWindowInstance;
        NativeRenderer rendererInstance;
        Properties propertiesInstance;
        Mouse mouseInstance;
        UI uiInstance;
        Engine engineInstance;
        Gizmo gizmoInstance;
        Bones bonesInstance;
        Animation animationInstance;
        VFXManager vfxInstance;
        ParticleManager particleInstance;
        LightManager lightsInstance;

        bool previousF10 = false;
};

#endif // SAS_APPLICATION_H
