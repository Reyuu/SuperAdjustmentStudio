#include "custom_shader_effect.h"
#include "application.h"
#include "custom_shader_catalog.h"
#include "logger.h"
#include "settings.h"

#include <windows.h>

#include <cstring>

namespace {

// seconds since the first frame, for gTime
double nowSeconds() {
    LARGE_INTEGER frequency = {};
    LARGE_INTEGER counter = {};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart == 0) {
        return 0.0;
    }
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

constexpr double RELOAD_INTERVAL_SECONDS = 1.0;

} // namespace

// seconds since the first frame, for gTime

CustomShaderEffect::~CustomShaderEffect() {
    shutdown();
}

void CustomShaderEffect::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : slots_) {
        if (entry.second) {
            entry.second->shutdown();
        }
    }
    slots_.clear();
    constants_.clear();
}

void CustomShaderEffect::onResize() {
    // slots hold no size dependent resources: the chain owns the ping-pong temps
}

void CustomShaderEffect::applyCpu(unsigned char* rgba, int w, int h) const {
    // no CPU equivalent of a user shader; PostChain::bakeGpu runs them on the GPU
    (void)rgba;
    (void)w;
    (void)h;
}

double CustomShaderEffect::elapsedTime() const {
    if (startTime_ < 0.0) {
        return 0.0;
    }
    return nowSeconds() - startTime_;
}

bool CustomShaderEffect::isEnabled() const {
    const auto& slots = Application::instance().settings().options.customShaders;
    for (const auto& slot : slots) {
        if (slot.enabled && !slot.ref.empty()) {
            return true;
        }
    }
    return false;
}

std::vector<CustomShaderCatalogEntry> CustomShaderEffect::catalog() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!examplesWritten_) {
        writeCustomShaderExamples();
        examplesWritten_ = true;
    }
    if (!catalogScanned_) {
        catalogEntries_ = scanCustomShaders();
        catalogScanned_ = true;
    }
    return catalogEntries_;
}

void CustomShaderEffect::rescan() {
    std::lock_guard<std::mutex> lock(mutex_);
    catalogEntries_ = scanCustomShaders();
    catalogScanned_ = true;
}

CustomShaderSlot* CustomShaderEffect::slotFor(const std::filesystem::path& absPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = slots_.find(absPath.string());
    return it == slots_.end() ? nullptr : it->second.get();
}

std::string CustomShaderEffect::errorFor(const std::filesystem::path& absPath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = slots_.find(absPath.string());
    if (it != slots_.end() && it->second) {
        return it->second->lastError();
    }
    return {};
}

void CustomShaderEffect::recompileAll(ID3D11Device* device) {
    if (!device) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : slots_) {
        if (!entry.second) {
            continue;
        }
        std::string error;
        entry.second->load(device, entry.second->path(), error);
    }
}

CustomShaderSlot* CustomShaderEffect::ensureSlot(ID3D11Device* device, const std::filesystem::path& absPath) {
    const std::string key = absPath.string();
    auto it = slots_.find(key);
    if (it == slots_.end()) {
        auto slot = std::make_unique<CustomShaderSlot>();
        std::string error;
        slot->load(device, absPath, error);
        it = slots_.emplace(key, std::move(slot)).first;
    }
    return it->second.get();
}

void CustomShaderEffect::pollReload(ID3D11Device* device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : slots_) {
        if (!entry.second || !entry.second->needsRecompile()) {
            continue;
        }
        std::string error;
        if (!entry.second->load(device, entry.second->path(), error)) {
            Logger->debug("custom_shader: reload of '{}' failed: {}", entry.first, error);
        }
    }
}

std::vector<CustomShaderEffect::ActiveSlot> CustomShaderEffect::collectActive(ID3D11Device* device) {
    const auto& options = Application::instance().settings().options.customShaders;
    std::vector<ActiveSlot> active;
    for (const auto& slotOptions : options) {
        if (!slotOptions.enabled || slotOptions.ref.empty()) {
            continue;
        }
        std::filesystem::path absPath;
        if (!resolveCustomShaderRef(slotOptions.ref, absPath)) {
            continue;
        }
        CustomShaderSlot* slot = ensureSlot(device, absPath);
        if (slot && slot->isReady()) {
            active.push_back({slot, &slotOptions});
        }
    }
    return active;
}

void CustomShaderEffect::applyGpu(ID3D11Device* device, ID3D11DeviceContext* context, PostFrame& frame) {
    if (!device || !context) {
        return;
    }

    const double now = nowSeconds();
    if (startTime_ < 0.0) {
        startTime_ = now;
        lastPollTime_ = now;
    }
    if (now - lastPollTime_ >= RELOAD_INTERVAL_SECONDS) {
        lastPollTime_ = now;
        pollReload(device);
    }

    const std::vector<ActiveSlot> active = collectActive(device);
    // pass-through default: nothing to run leaves the image untouched
    frame.outputSrv = frame.input;
    if (active.empty()) {
        return;
    }

    ID3D11ShaderResourceView* readSrv = frame.input;
    ID3D11RenderTargetView* writeRtv = frame.scratchRtv;
    ID3D11ShaderResourceView* writeSrv = frame.scratchSrv;

    const float time = (float)elapsedTime();
    for (size_t i = 0; i < active.size(); ++i) {
        const bool lastSlot = (i + 1 == active.size());
        const bool toBackbuffer = lastSlot && frame.isLast;
        ID3D11RenderTargetView* dst = toBackbuffer ? frame.backbuffer : writeRtv;

        unsigned flags = 0;
        if (frame.depthSrv != nullptr) {
            flags |= 1u; // hasDepth
        }
        if (toBackbuffer) {
            flags |= 2u; // isLastPass
        }
        flags |= (unsigned)(i & 0xFF) << 8; // passIndex

        std::vector<unsigned char> packed;
        {
            CustomShaderSlot* slot = active[i].slot;
            std::lock_guard<std::mutex> lock(mutex_);
            auto& constants = constants_[slot->path().string()];
            if (constants.size() != slot->cbSize()) {
                constants.assign(slot->cbSize(), 0);
            }
            slot->packConstants(constants.data(), *active[i].options, time, frame.width, frame.height, flags);
            packed = constants;
        }

        ID3D11ShaderResourceView* inputs[3] = {readSrv, nullptr, frame.depthSrv};
        if (!active[i].slot->draw(context, packed.data(), inputs, 3, dst, frame.width, frame.height)) {
            continue;
        }

        // unbind before ping-ponging: the texture bound as a target cannot be
        // read as a texture in the next pass
        ID3D11ShaderResourceView* unbindSrvs[3] = {nullptr, nullptr, nullptr};
        context->PSSetShaderResources(0, 3, unbindSrvs);
        ID3D11RenderTargetView* unbindRtv = nullptr;
        context->OMSetRenderTargets(1, &unbindRtv, nullptr);

        if (!lastSlot) {
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
}

bool CustomShaderEffect::bakeGpu(ID3D11Device* device, ID3D11DeviceContext* context, unsigned char* rgba, int w, int h, unsigned width, unsigned height) {
    if (!device || !context || !rgba || w <= 0 || h <= 0 || width == 0 || height == 0) {
        return false;
    }

    const std::vector<ActiveSlot> active = collectActive(device);
    if (active.empty()) {
        return false;
    }

    // source -> (tempA <-> tempB for the middle slots) -> result -> staging
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* source = nullptr;
    ID3D11ShaderResourceView* sourceSrv = nullptr;
    ID3D11Texture2D* tempA = nullptr;
    ID3D11ShaderResourceView* tempASrv = nullptr;
    ID3D11RenderTargetView* tempARtv = nullptr;
    ID3D11Texture2D* tempB = nullptr;
    ID3D11ShaderResourceView* tempBSrv = nullptr;
    ID3D11RenderTargetView* tempBRtv = nullptr;
    ID3D11Texture2D* result = nullptr;
    ID3D11ShaderResourceView* resultSrv = nullptr;
    ID3D11RenderTargetView* resultRtv = nullptr;
    ID3D11Texture2D* staging = nullptr;

    ID3D11RasterizerState* savedRasterizer = nullptr;
    ID3D11BlendState* savedBlend = nullptr;
    ID3D11DepthStencilState* savedDepthStencil = nullptr;
    UINT savedStencilRef = 0;
    FLOAT savedBlendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    UINT savedSampleMask = 0;
    D3D11_VIEWPORT savedViewport = {};
    UINT savedViewportCount = 1;
    ID3D11RasterizerState* rasterSolid = nullptr;
    ID3D11BlendState* blendOff = nullptr;
    ID3D11DepthStencilState* depthStencilOff = nullptr;

    bool ok = false;

    do {
        context->RSGetState(&savedRasterizer);
        context->OMGetBlendState(&savedBlend, savedBlendFactor, &savedSampleMask);
        context->OMGetDepthStencilState(&savedDepthStencil, &savedStencilRef);
        context->RSGetViewports(&savedViewportCount, &savedViewport);

        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blendDesc, &blendOff);

        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.ScissorEnable = FALSE;
        rasterDesc.DepthClipEnable = TRUE;
        device->CreateRasterizerState(&rasterDesc, &rasterSolid);

        D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.StencilEnable = FALSE;
        device->CreateDepthStencilState(&depthStencilDesc, &depthStencilOff);

        context->OMSetBlendState(blendOff, nullptr, 0xFFFFFFFF);
        context->RSSetState(rasterSolid);
        context->OMSetDepthStencilState(depthStencilOff, 0);

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &source))) {
            break;
        }

        {
            D3D11_TEXTURE2D_DESC uploadDesc = desc;
            uploadDesc.BindFlags = 0;
            uploadDesc.Usage = D3D11_USAGE_STAGING;
            uploadDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ID3D11Texture2D* upload = nullptr;
            if (FAILED(device->CreateTexture2D(&uploadDesc, nullptr, &upload))) {
                break;
            }
            D3D11_MAPPED_SUBRESOURCE uploadMapped = {};
            // D3D11_MAP_WRITE_DISCARD is buffers only, textures need D3D11_MAP_WRITE
            if (FAILED(context->Map(upload, 0, D3D11_MAP_WRITE, 0, &uploadMapped)) || !uploadMapped.pData) {
                upload->Release();
                break;
            }
            unsigned char* destination = static_cast<unsigned char*>(uploadMapped.pData);
            for (unsigned row = 0; row < height; ++row) {
                std::memcpy(destination + (size_t)row * uploadMapped.RowPitch, rgba + (size_t)row * (size_t)w * 4, (size_t)w * 4);
            }
            context->Unmap(upload, 0);
            context->CopyResource(source, upload);
            upload->Release();
        }
        if (FAILED(device->CreateShaderResourceView(source, nullptr, &sourceSrv))) {
            break;
        }
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &tempA)) || FAILED(device->CreateTexture2D(&desc, nullptr, &tempB)) ||
            FAILED(device->CreateTexture2D(&desc, nullptr, &result))) {
            break;
        }
        if (FAILED(device->CreateShaderResourceView(tempA, nullptr, &tempASrv)) || FAILED(device->CreateRenderTargetView(tempA, nullptr, &tempARtv)) ||
            FAILED(device->CreateShaderResourceView(tempB, nullptr, &tempBSrv)) || FAILED(device->CreateRenderTargetView(tempB, nullptr, &tempBRtv))) {
            break;
        }
        if (FAILED(device->CreateShaderResourceView(result, nullptr, &resultSrv)) || FAILED(device->CreateRenderTargetView(result, nullptr, &resultRtv))) {
            break;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.BindFlags = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
            break;
        }

        Logger->info("custom_shader: baking {} slot(s) over a {}x{} image", active.size(), width, height);

        ID3D11ShaderResourceView* readSrv = sourceSrv;
        ID3D11RenderTargetView* writeRtv = tempARtv;
        bool writeIsA = true;

        for (size_t i = 0; i < active.size(); ++i) {
            const bool lastSlot = (i + 1 == active.size());
            ID3D11RenderTargetView* dst = lastSlot ? resultRtv : writeRtv;

            unsigned flags = 0;
            if (lastSlot) {
                flags |= 2u; // isLastPass
            }
            flags |= (unsigned)(i & 0xFF) << 8; // passIndex

            CustomShaderSlot* slot = active[i].slot;
            std::vector<unsigned char> packed;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto& constants = constants_[slot->path().string()];
                if (constants.size() != slot->cbSize()) {
                    constants.assign(slot->cbSize(), 0);
                }
                // same elapsed time as the live view, so animated shaders look
                // the same in the saved shot as they did on screen
                slot->packConstants(constants.data(), *active[i].options, elapsedTime(), (unsigned)w, (unsigned)h, flags);
                packed = constants;
            }

            ID3D11ShaderResourceView* inputs[3] = {readSrv, nullptr, nullptr};
            if (!slot->draw(context, packed.data(), inputs, 3, dst, width, height)) {
                Logger->warn("custom_shader: bake draw {} failed for '{}'", i, slot->path().filename().string());
                continue;
            }

            ID3D11ShaderResourceView* unbindSrvs[3] = {nullptr, nullptr, nullptr};
            context->PSSetShaderResources(0, 3, unbindSrvs);
            ID3D11RenderTargetView* unbindRtv = nullptr;
            context->OMSetRenderTargets(1, &unbindRtv, nullptr);

            if (!lastSlot) {
                if (writeIsA) {
                    readSrv = tempASrv;
                    writeRtv = tempBRtv;
                } else {
                    readSrv = tempBSrv;
                    writeRtv = tempARtv;
                }
                writeIsA = !writeIsA;
            }
        }

        context->CopyResource(staging, result);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
            break;
        }
        const unsigned char* rows = static_cast<const unsigned char*>(mapped.pData);
        for (unsigned row = 0; row < height; ++row) {
            std::memcpy(rgba + (size_t)row * (size_t)w * 4, rows + (size_t)mapped.RowPitch * row, (size_t)w * 4);
        }
        context->Unmap(staging, 0);
        ok = true;
    } while (false);

    // give the game its state back before anything else runs on this thread
    ID3D11RenderTargetView* nullTargets[1] = {nullptr};
    context->OMSetRenderTargets(1, nullTargets, nullptr);
    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    context->PSSetShaderResources(0, 3, nullSrvs);
    if (savedBlend) {
        context->OMSetBlendState(savedBlend, savedBlendFactor, savedSampleMask);
    }
    if (savedRasterizer) {
        context->RSSetState(savedRasterizer);
    }
    if (savedDepthStencil) {
        context->OMSetDepthStencilState(savedDepthStencil, savedStencilRef);
    }
    if (savedViewportCount > 0) {
        context->RSSetViewports(savedViewportCount, &savedViewport);
    }

    void* temporaries[] = {depthStencilOff, blendOff, rasterSolid, savedDepthStencil, savedBlend, savedRasterizer};
    for (auto* resource : temporaries) {
        if (resource) {
            static_cast<IUnknown*>(resource)->Release();
        }
    }

    void* resources[] = {staging, resultSrv, resultRtv, result, tempBSrv, tempBRtv, tempB, tempASrv, tempARtv, tempA, sourceSrv, source};
    for (auto* resource : resources) {
        if (resource) {
            static_cast<IUnknown*>(resource)->Release();
        }
    }

    if (ok) {
        Logger->info("custom_shader: bake finished");
    } else {
        Logger->warn("custom_shader: bake failed, the image is saved unbaked");
    }
    return ok;
}
