#include "pass.h"

#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "logger.h"

FullscreenPass::~FullscreenPass() {
    shutdown();
}

void FullscreenPass::shutdown() {
    releaseDeviceObjects();
    releaseTemps();
}

void FullscreenPass::releaseDeviceObjects() {
    if (constantBuffer_) {
        constantBuffer_->Release();
        constantBuffer_ = nullptr;
    }
    if (samplerState_) {
        samplerState_->Release();
        samplerState_ = nullptr;
    }
    if (pixelShader_) {
        pixelShader_->Release();
        pixelShader_ = nullptr;
    }
    if (vertexShader_) {
        vertexShader_->Release();
        vertexShader_ = nullptr;
    }
}

void FullscreenPass::onResize() {
    releaseTemps();
}

bool FullscreenPass::initialize(ID3D11Device* device, const std::vector<PassShader>& shaders, unsigned cbSize, const std::string& pixelEntry) {
    if (!device) {
        return false;
    }
    shaders_ = shaders;
    pixelEntry_ = pixelEntry;
    releaseDeviceObjects();
    releaseTemps();

    if (!compileShaders(device)) {
        Logger->error("pass: shader compilation failed");
        return false;
    }
    if (!createSampler(device)) {
        Logger->error("pass: sampler creation failed");
        return false;
    }
    if (!createConstantBuffer(device, cbSize)) {
        Logger->error("pass: constant buffer creation failed");
        return false;
    }
    return true;
}

bool FullscreenPass::compileShaders(ID3D11Device* device) {
    for (const auto& s : shaders_) {
        ID3DBlob* errorBlob = nullptr;
        ID3DBlob* codeBlob = nullptr;
        HRESULT hr =
            D3DCompile(s.source.data(), s.source.size(), nullptr, nullptr, nullptr, s.entryPoint.c_str(), s.target.c_str(), 0, 0, &codeBlob, &errorBlob);
        if (FAILED(hr)) {
            std::string msg = errorBlob ? std::string((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize()) : "unknown";
            Logger->error("pass: D3DCompile {} ({}) failed: {}", s.entryPoint, s.target, msg);
            if (errorBlob) {
                errorBlob->Release();
            }
            return false;
        }
        if (errorBlob) {
            errorBlob->Release();
        }

        if (s.entryPoint == "VS") {
            if (FAILED(device->CreateVertexShader(codeBlob->GetBufferPointer(), codeBlob->GetBufferSize(), nullptr, &vertexShader_))) {
                codeBlob->Release();
                return false;
            }
        } else if (s.entryPoint == pixelEntry_) {
            if (FAILED(device->CreatePixelShader(codeBlob->GetBufferPointer(), codeBlob->GetBufferSize(), nullptr, &pixelShader_))) {
                codeBlob->Release();
                return false;
            }
        }
        codeBlob->Release();
    }
    return vertexShader_ != nullptr && pixelShader_ != nullptr;
}

bool FullscreenPass::createSampler(ID3D11Device* device) {
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(device->CreateSamplerState(&desc, &samplerState_));
}

bool FullscreenPass::createConstantBuffer(ID3D11Device* device, unsigned cbSize) {
    if (cbSize == 0) {
        return false;
    }
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = cbSize;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &constantBuffer_));
}

bool FullscreenPass::ensureTemps(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (tempW_ == width && tempH_ == height && tempFmt_ == format) {
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

bool FullscreenPass::ensureTempsIfNeeded(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (tempA_ && tempW_ == width && tempH_ == height && tempFmt_ == format) {
        return true;
    }
    return ensureTemps(device, width, height, format);
}

void FullscreenPass::releaseTemps() {
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

bool FullscreenPass::run(ID3D11DeviceContext* context, const void* cbData, unsigned cbSize, ID3D11ShaderResourceView* const* inputs, unsigned inputCount,
                         ID3D11RenderTargetView* outputRtv, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (!context || !constantBuffer_ || !vertexShader_ || !pixelShader_ || !outputRtv) {
        return false;
    }
    ID3D11Device* device = nullptr;
    HRESULT hr = context->QueryInterface(__uuidof(ID3D11Device), (void**)&device);
    if (FAILED(hr) || !device || !ensureTemps(device, width, height, format)) {
        if (device) {
            device->Release();
        }
        return false;
    }
    device->Release();

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vertexShader_, nullptr, 0);
    context->PSSetShader(pixelShader_, nullptr, 0);
    context->PSSetSamplers(0, 1, &samplerState_);
    context->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetConstantBuffers(0, 1, &constantBuffer_);

    ID3D11ShaderResourceView* srvs[3] = {nullptr, nullptr, nullptr};
    for (unsigned i = 0; i < inputCount && i < 3; ++i) {
        srvs[i] = inputs[i];
    }
    context->PSSetShaderResources(0, 3, srvs);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) {
        context->PSSetShaderResources(0, 3, srvs);
        return false;
    }
    memcpy(mapped.pData, cbData, cbSize);
    context->Unmap(constantBuffer_, 0);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<FLOAT>(width);
    viewport.Height = static_cast<FLOAT>(height);
    viewport.MaxDepth = 1.0f;
    context->OMSetRenderTargets(1, &outputRtv, nullptr);
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);

    // unbind SRVs so RTV-bound temp can be read as SRV in the next pass
    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    context->PSSetShaderResources(0, 3, nullSrvs);
    return true;
}
