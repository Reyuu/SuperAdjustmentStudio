// D3D11 swapchain hooking + ImGui overlay rendering.
// Present/ResizeBuffers are detoured through kiero (MinHook-based), so no
// manual vtable scanning or dummy-device plumbing is needed on our side.
#ifndef SAS_NATIVE_RENDERER_H
#define SAS_NATIVE_RENDERER_H
#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT SyncInterval, UINT Flags);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT BufferCount, UINT Width, UINT Height, \
                                                    DXGI_FORMAT NewFormat, UINT SwapChainFlags);

// https://github.com/eugen15/directx-present-hook
// thirdparty\imgui\examples\example_win32_directx11\main.cpp
class NativeRenderer {
    public:
        NativeRenderer();
        static NativeRenderer& instance() {
            return *instancePtr;
        }

        //setters
        void setDevice(ID3D11Device* device) {
            pd3dDevice = device;
        }
        void setContext(ID3D11DeviceContext* context) {
            pd3dContext = context;
        }
        void setRenderTargetView(ID3D11RenderTargetView* view) {
            pRenderTargetView = view;
        }
        void setUiRect(const RECT& rect) {
            uiRectData = rect;
        }

        //getters
        ID3D11Device* device() const {
            return pd3dDevice;
        }
        ID3D11DeviceContext* context() const {
            return pd3dContext;
        }
        ID3D11RenderTargetView* renderTargetView() const {
            return pRenderTargetView;
        }
        RECT uiRect() const {
            return uiRectData;
        }
        bool isImGuiInitialized() const {
            return isImGuiInitializedBool;
        }
        bool areHooksInstalled() const {
            return areHooksInstalledBool;
        }

        PresentFn origPresent() const { 
            return origPresentPointer;
        }
        ResizeBuffersFn origResizeBuffers() const {
            return origResizeBuffersPointer;
        }

        //functionality
        //  imgui context + win32/dx11 backend initialization from the live swapchain.
        bool initImGuiInGame(IDXGISwapChain* pSwapChain);
        //  resolve the D3D11 swapchain vtable via kiero and detour Present/ResizeBuffers.
        bool installHooks(PresentFn presentDetour, ResizeBuffersFn resizeBuffersDetour);
        //  restore the original vtable entries and tear kiero down.
        void removeHooks();
        //  lazy create the render target view from swapchain back buffer
        bool ensureRenderTarget(IDXGISwapChain* pSwapChain);
        //  release render target from the chain
        void releaseRenderTargetView();
        //  bind the render target before imgui frame
        void beginRender();
        //  release all and clear imgui flag
        void shutdown();
    private:
        static NativeRenderer* instancePtr;

        ID3D11Device* pd3dDevice = nullptr;
        ID3D11DeviceContext* pd3dContext = nullptr;
        ID3D11RenderTargetView* pRenderTargetView = nullptr;
        RECT uiRectData = {0,0,0,0};
        bool isImGuiInitializedBool = false;
        bool areHooksInstalledBool = false;

        PresentFn origPresentPointer = nullptr;
        ResizeBuffersFn origResizeBuffersPointer = nullptr;
};

#endif // SAS_NATIVE_RENDERER_H
