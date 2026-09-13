#include "lut_effect.h"
#include "lut_cpu.h"

#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "application.h"
#include "stb_image.h"
#include "util.h"

const char* LutEffect::lutHLSL() {
    return
#include "lut_shader_source.inc"
        ;
}

LutEffect::LutEffect(LutDepth& depthProvider) : depthProvider_(depthProvider) {
}
LutEffect::~LutEffect() = default;

bool LutEffect::initialize(ID3D11Device* device) {
    return ensureShaders(device);
}

void LutEffect::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    pass_.shutdown();
    if (depthViewPixelShader_) {
        depthViewPixelShader_->Release();
        depthViewPixelShader_ = nullptr;
    }
    for (auto& kv : textures_) {
        if (kv.second.srv) {
            kv.second.srv->Release();
            kv.second.srv = nullptr;
        }
        if (kv.second.texture) {
            kv.second.texture->Release();
            kv.second.texture = nullptr;
        }
    }
    textures_.clear();
}

void LutEffect::onResize() {
    pass_.onResize();
}

LutDepth& LutEffect::depth() {
    return depthProvider_;
}

bool& LutEffect::freezeDepth() {
    return freezeDepthState_;
}

void LutEffect::rescan() const {
    std::lock_guard<std::mutex> lock(mutex_);
    catalogEntries_ = scanSasLuts();
    catalogScanned_ = true;
    // drop cached LUTs whose files disappeared
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (GetFileAttributesA(it->first.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (it->second.srv) {
                it->second.srv->Release();
                it->second.srv = nullptr;
            }
            if (it->second.texture) {
                it->second.texture->Release();
                it->second.texture = nullptr;
            }
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

const std::vector<LutCatalogEntry>& LutEffect::catalog() const {
    return catalogEntries_;
}

void LutEffect::ensureCatalog() const {
    if (catalogScanned_) {
        return;
    }
    rescan();
}

void LutEffect::addLayer(const std::string& ref) {
    auto& layers = Application::instance().settings().options.lutLayers;
    if ((int)layers.size() >= LUT_MAX_LAYERS) {
        return;
    }
    LutLayerOptions o;
    o.ref = ref;
    layers.push_back(o);
    Application::instance().settings().markChanged();
}

void LutEffect::removeLayer(size_t index) {
    auto& layers = Application::instance().settings().options.lutLayers;
    if (index < layers.size()) {
        layers.erase(layers.begin() + index);
        Application::instance().settings().markChanged();
    }
}

bool LutEffect::resolveCatalog(const LutRef& ref, const std::filesystem::path& dir, LutCatalogEntry& out, std::string& absPath) const {
    std::filesystem::path p(ref.file);
    if (p.is_absolute() || ref.file.find('/') != std::string::npos || ref.file.find('\\') != std::string::npos) {
        absPath = ref.file;
    } else {
        absPath = (dir / ref.file).string();
    }

    const std::string bare = std::filesystem::path(absPath).filename().string();
    int wantSub = 0;
    if (ref.subIndex >= 0) {
        wantSub = ref.subIndex;
    }
    for (const auto& entry : catalogEntries_) {
        if (entry.kind != LutEntryType::Error && entry.filename == bare && entry.subIndex == wantSub) {
            if (entry.kind == LutEntryType::MLUTSub && ref.subIndex < 0) {
                continue;
            }
            out = entry;
            return true;
        }
    }
    return false;
}

LutEffect::LutTexture* LutEffect::ensureTexture(ID3D11Device* device, const std::string& absPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = textures_.find(absPath);
    if (it != textures_.end()) {
        return &it->second;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load(absPath.c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) {
            stbi_image_free(px);
        }
        return nullptr;
    }

    LutTexture t;
    t.width = w;
    t.height = h;
    t.pixels = std::make_shared<std::vector<unsigned char>>(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(w);
    desc.Height = static_cast<UINT>(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = t.pixels->data();
    initData.SysMemPitch = static_cast<UINT>(w) * 4;

    if (FAILED(device->CreateTexture2D(&desc, &initData, &t.texture))) {
        return nullptr;
    }
    if (FAILED(device->CreateShaderResourceView(t.texture, nullptr, &t.srv))) {
        t.texture->Release();
        return nullptr;
    }

    auto inserted = textures_.emplace(absPath, std::move(t));
    return &inserted.first->second;
}

bool LutEffect::ensureShaders(ID3D11Device* device) {
    if (pass_.vertexShader() && pass_.pixelShader() && depthViewPixelShader_ && pass_.sampler() && pass_.constantBuffer()) {
        return true;
    }

    const char* src = lutHLSL();
    std::vector<PassShader> shaders;
    shaders.push_back({std::string(src), "VS", "vs_4_0"});
    shaders.push_back({std::string(src), "PS", "ps_4_0"});

    if (!pass_.initialize(device, shaders, sizeof(LutEffectConstants))) {
        return false;
    }

    // depth-view pixel shader — separate entry, same source
    ID3DBlob* errorBlob = nullptr;
    ID3DBlob* dvBlob = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "DepthViewPS", "ps_4_0", 0, 0, &dvBlob, &errorBlob);
    if (FAILED(hr)) {
        std::string msg = errorBlob ? std::string((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize()) : "unknown";
        Logger->error("lut: D3DCompile DepthViewPS failed: {}", msg);
        if (errorBlob) {
            errorBlob->Release();
        }
        return false;
    }
    if (errorBlob) {
        errorBlob->Release();
    }
    if (FAILED(device->CreatePixelShader(dvBlob->GetBufferPointer(), dvBlob->GetBufferSize(), nullptr, &depthViewPixelShader_))) {
        dvBlob->Release();
        return false;
    }
    dvBlob->Release();
    return true;
}

// ---- per-frame apply --------------------------------------------------------

struct ResolvedLayer {
        LutLayerOptions options;
        LutEntryType kind = LutEntryType::Error;
        ID3D11ShaderResourceView* srv = nullptr;
        LutEffectConstants constants = {};
};

bool LutEffect::isEnabled() const {
    const auto& options = Application::instance().settings().options;
    if (options.lutDepthCapture) {
        return true;
    }
    for (const auto& o : options.lutLayers) {
        if (o.enabled && !o.ref.empty()) {
            return true;
        }
    }
    return false;
}

void LutEffect::applyGpu(ID3D11Device* device, ID3D11DeviceContext* context, PostFrame& frame) {
    if (!device || !context) {
        return;
    }
    ensureCatalog();
    if (!ensureShaders(device)) {
        return;
    }

    const std::filesystem::path dir = sasLutDirectory();
    const auto& globalOptions = Application::instance().settings().options;

    // pass-through default: no layers and no debug view leaves the image untouched.
    frame.outputSrv = frame.input;

    std::vector<ResolvedLayer> layers;
    {
        const auto& options = Application::instance().settings().options.lutLayers;
        for (const auto& o : options) {
            if (!o.enabled || o.ref.empty()) {
                continue;
            }
            LutRef ref;
            parseLutRef(o.ref, ref);
            LutCatalogEntry entry;
            std::string absPath;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!resolveCatalog(ref, dir, entry, absPath)) {
                    continue;
                }
            }
            LutEffect::LutTexture* texture = ensureTexture(device, absPath);
            if (!texture) {
                continue;
            }
            ResolvedLayer resolved;
            resolved.options = o;
            resolved.kind = entry.kind;
            resolved.srv = texture->srv;
            resolved.constants.mode = entry.kind == LutEntryType::Hald ? 0 : entry.kind == LutEntryType::HorizontalStrip ? 1 : 2;
            resolved.constants.level = entry.kind == LutEntryType::Hald ? entry.haldLevel : entry.stripSlice;
            resolved.constants.tilesPerRow = entry.stripTilesPerRow;
            resolved.constants.blend = o.blend;
            resolved.constants.chroma = o.chroma;
            resolved.constants.luma = o.luma;
            resolved.constants.useDepth = 0;
            resolved.constants.focus = o.depthFocus;
            resolved.constants.range = o.depthRange;
            resolved.constants.blendMin = o.blendMin;
            resolved.constants.blendMax = o.blendMax;
            resolved.constants.tileSize = entry.tileSize;
            resolved.constants.tileAmount = entry.tileAmount;
            resolved.constants.lutAmount = entry.lutAmount;
            resolved.constants.selector = entry.subIndex;
            layers.push_back(resolved);
        }
    }

    depthProvider_.setEnabled(globalOptions.lutDepthCapture);
    if (!globalOptions.lutDepthCapture) {
        depthProvider_.removeHook();
    }
    bool anyGated = false;
    for (const auto& layer : layers) {
        if (layer.options.useDepth) {
            anyGated = true;
            break;
        }
    }
    // per-layer preview/heat drives depth gate; global lutDepthShowTexture still
    // renders raw depth texture when no layers are active.
    const bool needCopy = globalOptions.lutDepthCapture && (anyGated ||
                                                            std::any_of(layers.begin(), layers.end(),
                                                                        [](const auto& l) {
                                                                            return l.options.previewDepth;
                                                                        }) ||
                                                            globalOptions.lutDepthShowTexture);
    ID3D11ShaderResourceView* depthSrv = nullptr;
    if (globalOptions.lutDepthCapture) {
        depthProvider_.installHook(context);
        depthSrv = depthProvider_.update(device, context, frame.width, frame.height, globalOptions.lutDepthSource, globalOptions.lutDepthEveryN,
                                         freezeDepthState_, globalOptions.lutDepthNear, globalOptions.lutDepthFar, globalOptions.lutDepthLinearize, needCopy);
        for (size_t li = 0; li < layers.size(); ++li) {
            auto& layer = layers[li];
            // preview forces its own gate so it works on any layer,
            // even when nothing else is gated.
            const bool gated = (layer.options.useDepth || layer.options.previewDepth) && depthSrv;
            layer.constants.useDepth = gated ? 1 : 0;
            layer.constants.preview = (layer.options.previewDepth && gated) ? 1 : 0;
            layer.constants.linMode = globalOptions.lutDepthLinearize ? 1 : 0;
            layer.constants.heat = layer.options.heatPreview ? 1 : 0;
            layer.constants.nearZ = globalOptions.lutDepthNear;
            layer.constants.farZ = globalOptions.lutDepthFar;
            layer.constants.invert = globalOptions.lutDepthInvert ? 1 : 0;
        }
    }

    if (layers.empty()) {
        if (globalOptions.lutDepthShowTexture && depthSrv) {
            ID3D11RenderTargetView* debugDst = frame.isLast ? frame.backbuffer : frame.scratchRtv;
            renderDepthView(device, context, depthSrv, debugDst, frame.width, frame.height, globalOptions.lutDepthLinearize, globalOptions.lutDepthNear,
                            globalOptions.lutDepthFar, globalOptions.lutDepthInvert, false);
            frame.outputSrv = frame.isLast ? nullptr : frame.scratchSrv;
        }
        return;
    }

    ID3D11VertexShader* vs = pass_.vertexShader();
    ID3D11PixelShader* ps = pass_.pixelShader();
    ID3D11SamplerState* samp = pass_.sampler();
    ID3D11Buffer* cb = pass_.constantBuffer();

    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(ps, nullptr, 0);
    context->PSSetSamplers(0, 1, &samp);
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(0, 1, &cb);

    ID3D11ShaderResourceView* readSrv = frame.input;
    ID3D11RenderTargetView* writeRtv = frame.scratchRtv;
    ID3D11ShaderResourceView* writeSrv = frame.scratchSrv;

    const size_t layerCount = layers.size();
    // preview short-circuit: gate visualization only. every other layer is
    // skipped so no LUT can leak into the preview image.
    size_t drawFirst = 0;
    size_t drawEnd = layerCount;
    for (size_t i = 0; i < layerCount; ++i) {
        if (layers[i].constants.preview != 0) {
            drawFirst = i;
            drawEnd = i + 1;
            break;
        }
    }
    for (size_t i = drawFirst; i < drawEnd; ++i) {
        const bool lastLayer = (i + 1 == drawEnd);
        ID3D11RenderTargetView* dst = (lastLayer && frame.isLast) ? frame.backbuffer : writeRtv;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(pass_.constantBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            break;
        }
        memcpy(mapped.pData, &layers[i].constants, sizeof(LutEffectConstants));
        context->Unmap(pass_.constantBuffer(), 0);

        ID3D11ShaderResourceView* depthSrvOrNull = nullptr;
        if (layers[i].constants.useDepth != 0) {
            depthSrvOrNull = depthSrv;
        }
        ID3D11ShaderResourceView* srvs[3] = {readSrv, layers[i].srv, depthSrvOrNull};
        context->PSSetShaderResources(0, 3, srvs);

        context->OMSetRenderTargets(1, &dst, nullptr);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<FLOAT>(frame.width);
        viewport.Height = static_cast<FLOAT>(frame.height);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        context->Draw(3, 0);

        // unbind SRVs before ping-ponging or the RTV-bound texture can't be
        // read as SRV in the next pass (black output).
        ID3D11ShaderResourceView* unbindSrvs[3] = {nullptr, nullptr, nullptr};
        context->PSSetShaderResources(0, 3, unbindSrvs);
        ID3D11RenderTargetView* unbindRtv = nullptr;
        context->OMSetRenderTargets(1, &unbindRtv, nullptr);

        if (!lastLayer) {
            // ping-pong between the chain's two temps
            if (readSrv == frame.input) {
                readSrv = frame.scratchSrv;
            } else {
                readSrv = frame.input;
            }
            if (writeRtv == frame.scratchRtv) {
                writeRtv = frame.inputRtv;
                writeSrv = frame.input;
            } else {
                writeRtv = frame.scratchRtv;
                writeSrv = frame.scratchSrv;
            }
        } else {
            frame.outputSrv = frame.isLast ? nullptr : writeSrv;
        }
    }

    if (globalOptions.lutDepthShowTexture && depthSrv) {
        ID3D11RenderTargetView* debugDst = frame.isLast ? frame.backbuffer : frame.scratchRtv;
        renderDepthView(device, context, depthSrv, debugDst, frame.width, frame.height, globalOptions.lutDepthLinearize, globalOptions.lutDepthNear,
                        globalOptions.lutDepthFar, globalOptions.lutDepthInvert, false);
        if (!frame.isLast) {
            frame.outputSrv = frame.scratchSrv;
        }
    }
}

void LutEffect::renderDepthView(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depthSrv, ID3D11RenderTargetView* dst, unsigned w,
                                unsigned h, bool linearize, float nearZ, float farZ, bool invert, bool heat) {
    if (!device || !context || !depthSrv || !dst || !ensureShaders(device)) {
        return;
    }
    LutEffectConstants constants = {};
    constants.linMode = linearize ? 1 : 0;
    constants.heat = heat ? 1 : 0;
    constants.nearZ = nearZ;
    constants.farZ = farZ;
    constants.invert = invert ? 1 : 0;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(pass_.constantBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(pass_.constantBuffer(), 0);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<FLOAT>(w);
    viewport.Height = static_cast<FLOAT>(h);
    viewport.MaxDepth = 1.0f;

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    ID3D11VertexShader* vs = pass_.vertexShader();
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(depthViewPixelShader_, nullptr, 0);
    ID3D11Buffer* cb = pass_.constantBuffer();
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(0, 1, &cb);

    ID3D11SamplerState* samp = pass_.sampler();
    context->PSSetSamplers(0, 1, &samp);
    ID3D11ShaderResourceView* srvs[3] = {nullptr, nullptr, depthSrv};
    context->PSSetShaderResources(0, 3, srvs);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* blendOff = nullptr;
    device->CreateBlendState(&blendDesc, &blendOff);

    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.ScissorEnable = FALSE;
    rasterDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rasterSolid = nullptr;
    device->CreateRasterizerState(&rasterDesc, &rasterSolid);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* dsOff = nullptr;
    device->CreateDepthStencilState(&dsDesc, &dsOff);

    context->OMSetBlendState(blendOff, nullptr, 0xFFFFFFFF);
    context->RSSetState(rasterSolid);
    context->OMSetDepthStencilState(dsOff, 0);

    context->OMSetRenderTargets(1, &dst, nullptr);
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);

    if (blendOff) {
        blendOff->Release();
    }
    if (rasterSolid) {
        rasterSolid->Release();
    }
    if (dsOff) {
        dsOff->Release();
    }

    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    context->PSSetShaderResources(0, 3, nullSrvs);
}

std::vector<LutCpuLayer> LutEffect::snapshot() const {
    ensureCatalog();
    std::vector<LutCpuLayer> out;
    const std::filesystem::path dir = sasLutDirectory();
    std::vector<LutLayerOptions> options = Application::instance().settings().options.lutLayers;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& o : options) {
        if (!o.enabled || o.ref.empty()) {
            continue;
        }
        LutRef ref;
        parseLutRef(o.ref, ref);
        LutCatalogEntry entry;
        std::string absPath;
        if (!resolveCatalog(ref, dir, entry, absPath)) {
            continue;
        }
        LutCpuLayer layer;
        layer.options = o;
        layer.kind = entry.kind;
        layer.level = entry.kind == LutEntryType::Hald ? entry.haldLevel : entry.stripSlice;
        layer.tilesPerRow = entry.stripTilesPerRow;
        layer.tileSize = entry.tileSize;
        layer.tileAmount = entry.tileAmount;
        layer.lutAmount = entry.lutAmount;
        layer.subIndex = entry.subIndex;
        auto it = textures_.find(absPath);
        if (it != textures_.end() && it->second.pixels) {
            layer.pixels = it->second.pixels;
            layer.w = it->second.width;
            layer.h = it->second.height;
            layer.valid = true;
        } else {
            int w = 0, h = 0, comp = 0;
            stbi_uc* px = stbi_load(absPath.c_str(), &w, &h, &comp, 4);
            if (!px || w <= 0 || h <= 0) {
                if (px) {
                    stbi_image_free(px);
                }
                continue;
            }
            layer.pixels = std::make_shared<std::vector<unsigned char>>(px, px + static_cast<size_t>(w) * h * 4);
            stbi_image_free(px);
            layer.w = w;
            layer.h = h;
            layer.valid = true;
        }
        out.push_back(std::move(layer));
    }
    return out;
}

void LutEffect::applyCpu(unsigned char* rgba, int w, int h) const {
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }
    auto layers = snapshot();
    if (layers.empty()) {
        return;
    }
    auto depthSnap = depthProvider_.cpuSnapshot();
    const bool invert = Application::instance().settings().options.lutDepthInvert;
    static const CpuDepth emptyDepth;
    applyLutCpuLayer(layers, rgba, w, h, depthSnap ? *depthSnap : emptyDepth, invert);
}
