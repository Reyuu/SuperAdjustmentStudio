#ifndef SAS_APPLICATION_H
#define SAS_APPLICATION_H

#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <atomic>
#include <thread>
#include "logger.h"
#include "sdk.h"
#include "hook_manager.h"
#include "game_window.h"
#include "native_renderer.h"
#include "props.h"
#include "mouse.h"
#include "ui.h"
#include "engine.h"
#include "gizmo.h"
#include "bones.h"
#include "animation.h"

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

        NativeRenderer& renderer(){
            return rendererInstance;
        }

        Properties& properties() {
            return propertiesInstance;
        }
        
        Mouse& mouse() {
            return mouseInstance;
        }
        
        UI& ui(){
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
        bool attach(ISharedProxyInterface* proxy);
        void detach();
    private:
        static HRESULT STDMETHODCALLTYPE presentDetour(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT STDMETHODCALLTYPE resizeBuffersDetour(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
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

        bool previousF10 = false;
};

#endif // SAS_APPLICATION_H
