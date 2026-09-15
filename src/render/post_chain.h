#ifndef SAS_POST_CHAIN_H
#define SAS_POST_CHAIN_H

#include "dxgi.h"
#include "d3d11.h"
#include <vector>
#include <memory>

#include "lut_depth.h"

struct PostFrame {
        ID3D11ShaderResourceView* input = nullptr;
        ID3D11RenderTargetView* inputRtv = nullptr;
        ID3D11ShaderResourceView* scratchSrv = nullptr;
        ID3D11RenderTargetView* scratchRtv = nullptr;
        ID3D11RenderTargetView* backbuffer = nullptr;
        ID3D11ShaderResourceView* depthSrv = nullptr;
        ID3D11ShaderResourceView* outputSrv = nullptr;
        unsigned width = 0;
        unsigned height = 0;
        unsigned long long frameIndex = 0;
        bool isLast = false;
};

class IEffect {
    public:
        virtual ~IEffect() = default;
        virtual bool isEnabled() const = 0;
        virtual void applyGpu(ID3D11Device* device, ID3D11DeviceContext* context, PostFrame& frame) = 0;
        virtual void applyCpu(unsigned char* rgba, int w, int h) const = 0;
        virtual void renderUI() = 0;
        virtual void onResize() = 0;
        virtual void shutdown() = 0;
};

class PostChain {
    public:
        PostChain();
        bool applyGpu(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv);
        void applyCpu(unsigned char* rgba, int w, int h) const;
        void renderUI();
        void onResize();
        void shutdown();
        void removeDepthHook();

    private:
        std::vector<std::unique_ptr<IEffect>> nodes_;

        unsigned long long frameCounter_ = 0;
        ID3D11Texture2D* tempA_ = nullptr;
        ID3D11Texture2D* tempB_ = nullptr;
        ID3D11ShaderResourceView* tempSrvA_ = nullptr;
        ID3D11ShaderResourceView* tempSrvB_ = nullptr;
        ID3D11RenderTargetView* tempRtvA_ = nullptr;
        ID3D11RenderTargetView* tempRtvB_ = nullptr;
        unsigned tempW_ = 0;
        unsigned tempH_ = 0;
        DXGI_FORMAT tempFmt_ = DXGI_FORMAT_UNKNOWN;

        LutDepth depth_;

        bool ensureTempsIfNeeded(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format);
        void releaseTemps();
        void saveD3DState(ID3D11DeviceContext* context, D3D11_VIEWPORT* outViewport, UINT* outViewportCount, ID3D11RasterizerState** outRasterizer,
                          ID3D11DepthStencilState** outDepthStencil, UINT* outStencilRef);
        void restoreD3DState(ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv, const D3D11_VIEWPORT* viewport, UINT viewportCount,
                             ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthStencil, UINT stencilRef, ID3D11BlendState* blend,
                             ID3D11RasterizerState* raster, ID3D11DepthStencilState* depthStencilState);

        void installPostState(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11BlendState** outBlend, ID3D11RasterizerState** outRaster,
                              ID3D11DepthStencilState** outDs);
        void unbindSrvs(ID3D11DeviceContext* context);
};
#endif // SAS_POST_CHAIN_H
