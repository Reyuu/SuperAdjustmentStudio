#include "post_chain.h"

#include <vector>
#include "dxgi.h"
#include "d3d11.h"

#include "lut_effect.h"

PostChain::PostChain() {
    nodes_.push_back(std::make_unique<LutEffect>(depth_));
}

bool PostChain::applyGpu(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv) {
    if (!swapChain || !device || !context || !backbufferRtv) {
        return false;
    }

    std::vector<IEffect*> active;
    for (auto& n : nodes_) {
        if (n->isEnabled()) {
            active.push_back(n.get());
        }
    }

    if (active.empty()) {
        depth_.setEnabled(false);
        depth_.removeHook();
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
        return false;
    }
    D3D11_TEXTURE2D_DESC backBufferDesc = {};
    backBuffer->GetDesc(&backBufferDesc);
    backBuffer->Release();
    backBuffer = nullptr;

    const unsigned w = backBufferDesc.Width;
    const unsigned h = backBufferDesc.Height;
    // ensure ping-pong temp textures exist before streaming the scene into them
    if (!ensureTempsIfNeeded(device, w, h, DXGI_FORMAT_R8G8B8A8_UNORM)) {
        return false;
    }

    ID3D11Texture2D* sceneBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&sceneBuffer))) || !sceneBuffer) {
        return false;
    }
    context->CopyResource(tempA_, sceneBuffer);
    sceneBuffer->Release();

    D3D11_VIEWPORT savedViewport = {};
    UINT viewportCount = 1;
    ID3D11RasterizerState* savedRasterizerState = nullptr;
    ID3D11DepthStencilState* savedDepthStencilState = nullptr;
    UINT savedStencilRef = 0;

    saveD3DState(context, &savedViewport, &viewportCount, &savedRasterizerState, &savedDepthStencilState, &savedStencilRef);

    ID3D11BlendState* blendOff = nullptr;
    ID3D11RasterizerState* rasterSolid = nullptr;
    ID3D11DepthStencilState* depthStencilOff = nullptr;
    installPostState(device, context, &blendOff, &rasterSolid, &depthStencilOff);

    PostFrame frame{};
    frame.width = w;
    frame.height = h;
    frame.backbuffer = backbufferRtv;
    frame.input = tempSrvA_;
    frame.depthSrv = nullptr;
    frame.outputSrv = nullptr;
    frame.frameIndex = frameCounter_++;

    for (size_t i = 0; i < active.size(); ++i) {
        frame.isLast = (i + 1 == active.size());
        if (frame.input == tempSrvA_) {
            frame.inputRtv = tempRtvA_;
            frame.scratchSrv = tempSrvB_;
            frame.scratchRtv = tempRtvB_;
        } else {
            frame.inputRtv = tempRtvB_;
            frame.scratchSrv = tempSrvA_;
            frame.scratchRtv = tempRtvA_;
        }

        frame.outputSrv = nullptr;
        active[i]->applyGpu(device, context, frame);
        unbindSrvs(context);
        if (!frame.isLast) {
            frame.input = frame.outputSrv;
        }
    }
    restoreD3DState(context, backbufferRtv, &savedViewport, viewportCount, savedRasterizerState, savedDepthStencilState, savedStencilRef, blendOff, rasterSolid,
                    depthStencilOff);
    return true;
}

void PostChain::applyCpu(unsigned char* rgba, int w, int h) const {
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }
    for (const auto& n : nodes_) {
        n->applyCpu(rgba, w, h);
    }
}

void PostChain::renderUI() {
    for (auto& n : nodes_) {
        n->renderUI();
    }
}

void PostChain::onResize() {
    releaseTemps();
    depth_.onResize();
    for (auto& n : nodes_) {
        n->onResize();
    }
}

void PostChain::shutdown() {
    for (auto& n : nodes_) {
        n->shutdown();
    }
    depth_.shutdown();
    releaseTemps();
}

void PostChain::removeDepthHook() {
    depth_.removeHook();
}

bool PostChain::ensureTempsIfNeeded(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (tempA_ && tempW_ == width && tempH_ == height && tempFmt_ == format) {
        return true;
    }

    releaseTemps();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    for (int i = 0; i < 2; ++i) {
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &tex))) {
            releaseTemps();
            return false;
        }
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(device->CreateShaderResourceView(tex, nullptr, &srv)) || FAILED(device->CreateRenderTargetView(tex, nullptr, &rtv))) {
            if (srv) {
                srv->Release();
            }
            if (rtv) {
                rtv->Release();
            }
            tex->Release();
            releaseTemps();
            return false;
        }
        if (i == 0) {
            tempA_ = tex;
            tempSrvA_ = srv;
            tempRtvA_ = rtv;
        } else {
            tempB_ = tex;
            tempSrvB_ = srv;
            tempRtvB_ = rtv;
        }
    }
    tempW_ = width;
    tempH_ = height;
    tempFmt_ = format;
    return true;
}

void PostChain::releaseTemps() {
    if (tempSrvA_) {
        tempSrvA_->Release();
        tempSrvA_ = nullptr;
    }
    if (tempRtvA_) {
        tempRtvA_->Release();
        tempRtvA_ = nullptr;
    }
    if (tempA_) {
        tempA_->Release();
        tempA_ = nullptr;
    }

    if (tempSrvB_) {
        tempSrvB_->Release();
        tempSrvB_ = nullptr;
    }
    if (tempRtvB_) {
        tempRtvB_->Release();
        tempRtvB_ = nullptr;
    }
    if (tempB_) {
        tempB_->Release();
        tempB_ = nullptr;
    }

    tempW_ = 0;
    tempH_ = 0;
    tempFmt_ = DXGI_FORMAT_UNKNOWN;
}

void PostChain::saveD3DState(ID3D11DeviceContext* context, D3D11_VIEWPORT* outViewport, UINT* outViewportCount, ID3D11RasterizerState** outRasterizer,
                             ID3D11DepthStencilState** outDepthStencil, UINT* outStencilRef) {
    context->RSGetViewports(outViewportCount, outViewport);
    context->RSGetState(outRasterizer);
    context->OMGetDepthStencilState(outDepthStencil, outStencilRef);
}

void PostChain::restoreD3DState(ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv, const D3D11_VIEWPORT* viewport, UINT viewportCount,
                                ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthStencil, UINT stencilRef, ID3D11BlendState* blend,
                                ID3D11RasterizerState* raster, ID3D11DepthStencilState* depthStencilState) {
    context->OMSetRenderTargets(1, &backbufferRtv, nullptr);
    context->RSSetViewports(viewportCount, viewport);
    context->RSSetState(rasterizer);
    context->OMSetDepthStencilState(depthStencil, stencilRef);

    if (blend) {
        blend->Release();
    }
    if (raster) {
        raster->Release();
    }
    if (depthStencilState) {
        depthStencilState->Release();
    }
    if (rasterizer) {
        rasterizer->Release();
    }
    if (depthStencil) {
        depthStencil->Release();
    }

    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    context->PSSetShaderResources(0, 3, nullSrvs);
}

void PostChain::installPostState(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11BlendState** outBlend, ID3D11RasterizerState** outRaster,
                                 ID3D11DepthStencilState** outDs) {
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, outBlend);

    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.ScissorEnable = FALSE;
    rasterDesc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rasterDesc, outRaster);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;
    device->CreateDepthStencilState(&dsDesc, outDs);

    context->OMSetBlendState(*outBlend, nullptr, 0xFFFFFFFF);
    context->RSSetState(*outRaster);
    context->OMSetDepthStencilState(*outDs, 0);
}

void PostChain::unbindSrvs(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    context->PSSetShaderResources(0, 3, nullSrvs);
    ID3D11RenderTargetView* unbindRtv = nullptr;
    context->OMSetRenderTargets(1, &unbindRtv, nullptr);
}