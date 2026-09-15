#include "lut_depth.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "minhook/include/MinHook.h"

// probe-verified on dev machine
static constexpr int OM_SET_RENDER_TARGETS_IDX = 33;
static constexpr int OM_SET_RENDER_TARGETS_UAV_IDX = 34;
static constexpr int CLEAR_DEPTH_STENCIL_IDX = 53;

// selection tuning
static constexpr size_t MAX_CANDIDATES = 24;
static constexpr unsigned long long STALE_FRAMES = 600;
static constexpr unsigned long long DECIDE_INTERVAL_MS = 5000;
static constexpr double FLAT_SPREAD_EPS = 1e-3;
static constexpr double SIZE_BONUS = 10.0;
static constexpr double CLEAR_BONUS = 5.0;

typedef void(STDMETHODCALLTYPE* OmSetRenderTargetsFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* OmSetRenderTargetsUavFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT,
                                                         ID3D11UnorderedAccessView* const*, const UINT*);
typedef void(STDMETHODCALLTYPE* ClearDepthStencilFn)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);

// hook originals + state. file-local so the LHS names don't leak to other TU.
namespace {
OmSetRenderTargetsFn origOmSetTargets = nullptr;
OmSetRenderTargetsUavFn origOmSetTargetsUav = nullptr;
ClearDepthStencilFn origClearDepthStencil = nullptr;
LutDepth* depthOwner = nullptr;
std::atomic<bool> captureEnabled{false};
} // namespace

void lutDepthHookCache(LutDepth* owner, ID3D11DepthStencilView* dsv, bool cleared);

static void STDMETHODCALLTYPE hookOmSetRenderTargets(ID3D11DeviceContext* self, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    if (origOmSetTargets) {
        origOmSetTargets(self, n, rtvs, dsv);
    }
    if (captureEnabled.load(std::memory_order_relaxed)) {
        lutDepthHookCache(depthOwner, dsv, false);
    }
}

static void STDMETHODCALLTYPE hookOmSetRenderTargetsUav(ID3D11DeviceContext* self, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv,
                                                        UINT uavStart, UINT uavCount, ID3D11UnorderedAccessView* const* uavs, const UINT* initCounts) {
    if (origOmSetTargetsUav) {
        origOmSetTargetsUav(self, n, rtvs, dsv, uavStart, uavCount, uavs, initCounts);
    }
    if (captureEnabled.load(std::memory_order_relaxed)) {
        lutDepthHookCache(depthOwner, dsv, false);
    }
}

static void STDMETHODCALLTYPE hookClearDepthStencil(ID3D11DeviceContext* self, ID3D11DepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) {
    if (origClearDepthStencil) {
        origClearDepthStencil(self, dsv, flags, depth, stencil);
    }
    if ((flags & D3D11_CLEAR_DEPTH) && captureEnabled.load(std::memory_order_relaxed)) {
        lutDepthHookCache(depthOwner, dsv, true);
    }
}

void lutDepthHookCache(LutDepth* owner, ID3D11DepthStencilView* dsv, bool cleared) {
    if (owner && dsv) {
        owner->cacheDsv(dsv, cleared);
    }
}

LutDepth::~LutDepth() {
    shutdown();
}

void LutDepth::setEnabled(bool enabled) {
    captureEnabled.store(enabled, std::memory_order_relaxed);
}

void LutDepth::cacheDsv(ID3D11DepthStencilView* dsv, bool cleared) {
    if (!dsv) {
        return; // end-of-frame unbinds must not wipe anything
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto it = candidates.find(dsv);
    if (it == candidates.end()) {
        if (candidates.size() >= MAX_CANDIDATES) {
            auto oldest = candidates.begin();
            for (auto o = candidates.begin(); o != candidates.end(); ++o) {
                if (o->second.lastSeenFrame < oldest->second.lastSeenFrame) {
                    oldest = o;
                }
            }
            if (oldest->first == selected) {
                selected = nullptr;
            }
            oldest->second.dsv->Release();
            candidates.erase(oldest);
        }
        Candidate c;
        c.dsv = dsv;
        dsv->AddRef();
        ID3D11Resource* res = nullptr;
        dsv->GetResource(&res);
        if (res) {
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(res->QueryInterface(IID_PPV_ARGS(&tex))) && tex) {
                D3D11_TEXTURE2D_DESC dd{};
                tex->GetDesc(&dd);
                c.w = dd.Width;
                c.h = dd.Height;
                c.fmt = dd.Format;
                tex->Release();
            }
            res->Release();
        }
        c.lastSeenFrame = currentFrame;
        it = candidates.emplace(dsv, c).first;
        ++candidateVersion;
    }
    it->second.lastSeenFrame = currentFrame;
    if (cleared) {
        ++it->second.clears;
    } else {
        ++it->second.binds;
    }
}

void LutDepth::installHook(ID3D11DeviceContext* context) {
    if (hookInstalled || hookFailed || !context) {
        return;
    }
    void** vtable = *(void***)context;
    hookTargets[0] = vtable[OM_SET_RENDER_TARGETS_IDX];
    hookTargets[1] = vtable[OM_SET_RENDER_TARGETS_UAV_IDX];
    hookTargets[2] = vtable[CLEAR_DEPTH_STENCIL_IDX];
    if (MH_CreateHook(hookTargets[0], (void*)&hookOmSetRenderTargets, (void**)&origOmSetTargets) != MH_OK || MH_EnableHook(hookTargets[0]) != MH_OK) {
        Logger->error("lutdepth: OMSetRenderTargets hook failed");
        hookFailed = true;
        return;
    }
    if (MH_CreateHook(hookTargets[1], (void*)&hookOmSetRenderTargetsUav, (void**)&origOmSetTargetsUav) != MH_OK || MH_EnableHook(hookTargets[1]) != MH_OK) {
        Logger->error("lutdepth: OMSetRenderTargetsAndUAVs hook failed");
        MH_DisableHook(hookTargets[0]);
        MH_RemoveHook(hookTargets[0]);
        origOmSetTargets = nullptr;
        hookFailed = true;
        return;
    }
    if (MH_CreateHook(hookTargets[2], (void*)&hookClearDepthStencil, (void**)&origClearDepthStencil) != MH_OK || MH_EnableHook(hookTargets[2]) != MH_OK) {
        Logger->error("lutdepth: ClearDepthStencilView hook failed (non-fatal)");
        hookTargets[2] = nullptr;
        origClearDepthStencil = nullptr;
    }
    depthOwner = this;
    hookInstalled = true;
    Logger->debug("lutdepth: hooks installed");
}

void LutDepth::removeHook() {
    if (!hookInstalled) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (hookTargets[i]) {
            MH_DisableHook(hookTargets[i]);
            MH_RemoveHook(hookTargets[i]);
            hookTargets[i] = nullptr;
        }
    }
    origOmSetTargets = nullptr;
    origOmSetTargetsUav = nullptr;
    origClearDepthStencil = nullptr;
    depthOwner = nullptr;
    hookInstalled = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (selected) {
        selected->Release();
        selected = nullptr;
    }
}

void LutDepth::shutdown() {
    std::lock_guard<std::mutex> lock(mutex);
    if (copySrv) {
        copySrv->Release();
        copySrv = nullptr;
    }
    if (copyTex) {
        copyTex->Release();
        copyTex = nullptr;
    }
    if (staging) {
        staging->Release();
        staging = nullptr;
    }
    copySrvFmt = DXGI_FORMAT_UNKNOWN;
    stagingFmt = DXGI_FORMAT_UNKNOWN;
    copyW = copyH = stagingW = stagingH = 0;
    for (auto& kv : candidates) {
        kv.second.dsv->Release();
    }
    candidates.clear();
    if (selected) {
        selected->Release();
        selected = nullptr;
    }
    cpu.reset();
}

void LutDepth::onResize() {
    std::lock_guard<std::mutex> lock(mutex);
    if (copySrv) {
        copySrv->Release();
        copySrv = nullptr;
    }
    if (copyTex) {
        copyTex->Release();
        copyTex = nullptr;
    }
    if (staging) {
        staging->Release();
        staging = nullptr;
    }
    copySrvFmt = DXGI_FORMAT_UNKNOWN;
    stagingFmt = DXGI_FORMAT_UNKNOWN;
    copyW = copyH = stagingW = stagingH = 0;
}

bool LutDepth::ensureCopy(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv) {
    if (!dsv) {
        return false;
    }
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    if (!res) {
        return false;
    }
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = res->QueryInterface(IID_PPV_ARGS(&tex));
    res->Release();
    if (FAILED(hr) || !tex) {
        return false;
    }
    D3D11_TEXTURE2D_DESC dd{};
    tex->GetDesc(&dd);
    DXGI_FORMAT copyFmt = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srvFmt = DXGI_FORMAT_UNKNOWN;
    if (dd.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || dd.Format == DXGI_FORMAT_R24G8_TYPELESS) {
        copyFmt = DXGI_FORMAT_R24G8_TYPELESS;
        srvFmt = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    } else if (dd.Format == DXGI_FORMAT_D32_FLOAT || dd.Format == DXGI_FORMAT_R32_TYPELESS) {
        copyFmt = DXGI_FORMAT_R32_TYPELESS;
        srvFmt = DXGI_FORMAT_R32_FLOAT;
    } else if (dd.Format == DXGI_FORMAT_D16_UNORM || dd.Format == DXGI_FORMAT_R16_TYPELESS) {
        copyFmt = DXGI_FORMAT_R16_TYPELESS;
        srvFmt = DXGI_FORMAT_R16_UNORM;
    } else {
        tex->Release();
        return false;
    }
    const bool msaa = dd.SampleDesc.Count > 1;
    if (!copyTex || copyW != dd.Width || copyH != dd.Height || copySrvFmt != srvFmt) {
        if (copySrv) {
            copySrv->Release();
            copySrv = nullptr;
        }
        if (copyTex) {
            copyTex->Release();
            copyTex = nullptr;
        }
        D3D11_TEXTURE2D_DESC cd{};
        cd.Width = dd.Width;
        cd.Height = dd.Height;
        cd.MipLevels = 1;
        cd.ArraySize = 1;
        cd.Format = copyFmt;
        cd.SampleDesc.Count = 1;
        cd.Usage = D3D11_USAGE_DEFAULT;
        cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&cd, nullptr, &copyTex))) {
            tex->Release();
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = srvFmt;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(copyTex, &sv, &copySrv))) {
            copyTex->Release();
            copyTex = nullptr;
            tex->Release();
            return false;
        }
        copyW = dd.Width;
        copyH = dd.Height;
        copySrvFmt = srvFmt;
    }
    if (msaa) {
        context->ResolveSubresource(copyTex, 0, tex, 0, dd.Format);
    } else {
        context->CopyResource(copyTex, tex);
    }
    tex->Release();
    return copySrv != nullptr;
}

// measure current copyTex content. Render thread only.
static bool measureCopy(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* copyTex, DXGI_FORMAT srvFmt, unsigned w, unsigned h,
                        ID3D11Texture2D*& staging, DXGI_FORMAT& stagingFmt, unsigned& stagingW, unsigned& stagingH, double& mn, double& mx, double& mean) {
    mn = 1.0;
    mx = 0.0;
    mean = 0.0;
    if (!copyTex || w == 0 || h == 0) {
        return false;
    }
    DXGI_FORMAT wantFmt = DXGI_FORMAT_UNKNOWN;
    if (srvFmt == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) {
        wantFmt = DXGI_FORMAT_R24G8_TYPELESS;
    } else if (srvFmt == DXGI_FORMAT_R32_FLOAT) {
        wantFmt = DXGI_FORMAT_R32_TYPELESS;
    } else if (srvFmt == DXGI_FORMAT_R16_UNORM) {
        wantFmt = DXGI_FORMAT_R16_TYPELESS;
    } else {
        return false;
    }
    if (!staging || stagingFmt != wantFmt || stagingW != w || stagingH != h) {
        if (staging) {
            staging->Release();
            staging = nullptr;
        }
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = w;
        sd.Height = h;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.Format = wantFmt;
        if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging))) {
            return false;
        }
        stagingFmt = wantFmt;
        stagingW = w;
        stagingH = h;
    }
    context->CopyResource(staging, copyTex);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
        return false;
    }
    double sum = 0.0;
    size_t n = 0;
    const unsigned char* base = static_cast<const unsigned char*>(mapped.pData);
    for (unsigned y = 0; y < h; y += 16) {
        const unsigned char* row = base + (size_t)y * mapped.RowPitch;
        for (unsigned x = 0; x < w; x += 16) {
            double v = 0.0;
            if (srvFmt == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) {
                const unsigned char* px = row + (size_t)x * 4;
                const unsigned raw = (unsigned)px[0] | ((unsigned)px[1] << 8) | ((unsigned)px[2] << 16);
                v = raw / 16777215.0;
            } else if (srvFmt == DXGI_FORMAT_R32_FLOAT) {
                v = *(const float*)(row + (size_t)x * 4);
            } else {
                const unsigned raw = *(const unsigned short*)(row + (size_t)x * 2);
                v = raw / 65535.0;
            }
            if (v < 0.0 || v > 1.0 || v != v) {
                continue;
            }
            mn = v < mn ? v : mn;
            mx = v > mx ? v : mx;
            sum += v;
            ++n;
        }
    }
    context->Unmap(staging, 0);
    if (n == 0) {
        return false;
    }
    mean = sum / n;
    return true;
}

std::vector<LutDepth::Candidate> LutDepth::sortedSnapshot(std::mutex& mutex, std::map<ID3D11DepthStencilView*, LutDepth::Candidate>& map, bool addRef) {
    std::vector<const LutDepth::Candidate*> sorted;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& kv : map) {
            if (kv.second.w != 0 && kv.second.h != 0) {
                sorted.push_back(&kv.second);
            }
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const LutDepth::Candidate* a, const LutDepth::Candidate* b) {
        if (a->w != b->w) {
            return a->w > b->w;
        }
        if (a->h != b->h) {
            return a->h > b->h;
        }
        if (a->fmt != b->fmt) {
            return a->fmt < b->fmt;
        }
        return a->dsv < b->dsv;
    });
    std::vector<LutDepth::Candidate> out;
    for (const auto* c : sorted) {
        if (addRef) {
            c->dsv->AddRef();
        }
        out.push_back(*c);
    }
    return out;
}

std::vector<DepthSourceInfo> LutDepth::sourceList() {
    std::vector<DepthSourceInfo> out;
    auto sorted = sortedSnapshot(mutex, candidates, true);
    int occurrence = 0;
    int lastW = -1, lastH = -1, lastFmt = -1;
    for (auto& c : sorted) {
        if ((int)c.w == lastW && (int)c.h == lastH && (int)c.fmt == lastFmt) {
            ++occurrence;
        } else {
            occurrence = 0;
            lastW = (int)c.w;
            lastH = (int)c.h;
            lastFmt = (int)c.fmt;
        }
        DepthSourceInfo info;
        char key[64] = {};
        snprintf(key, sizeof(key), "%ux%u:%d@%d", c.w, c.h, (int)c.fmt, occurrence);
        info.key = key;
        char label[192] = {};
        if (c.lastSpread >= 0.0) {
            snprintf(label, sizeof(label), "%ux%u f%d  clears=%u binds=%u  spread=%.3f", c.w, c.h, (int)c.fmt, c.clears, c.binds, c.lastSpread);
        } else {
            snprintf(label, sizeof(label), "%ux%u f%d  clears=%u binds=%u", c.w, c.h, (int)c.fmt, c.clears, c.binds);
        }
        info.label = label;
        out.push_back(info);
        c.dsv->Release();
    }
    return out;
}

std::shared_ptr<const CpuDepth> LutDepth::cpuSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return cpu;
}

bool LutDepth::decide(ID3D11Device* device, ID3D11DeviceContext* context, unsigned bw, unsigned bh, const std::string& sourceKey) {
    lastDecideTick = GetTickCount64();
    lastDecideKey = sourceKey;
    // "WxH:F@k" in sourceList order.
    if (!sourceKey.empty()) {
        unsigned mw = 0, mh = 0;
        int mf = 0, mk = 0;
        if (sscanf(sourceKey.c_str(), "%ux%u:%d@%d", &mw, &mh, &mf, &mk) == 4) {
            auto sorted = sortedSnapshot(mutex, candidates, true);
            int occurrence = 0;
            ID3D11DepthStencilView* found = nullptr;
            for (auto& c : sorted) {
                if (c.w == mw && c.h == mh && (int)c.fmt == mf) {
                    if (occurrence == mk && currentFrame - c.lastSeenFrame < STALE_FRAMES) {
                        found = c.dsv;
                        break;
                    }
                    ++occurrence;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (found) {
                    if (selected != found) {
                        if (selected) {
                            selected->Release();
                        }
                        selected = found;
                        found->AddRef();
                        Logger->debug("lutdepth: manual source {}", sourceKey);
                    }
                    decidedVersion = candidateVersion;
                }
            }
            for (auto& c : sorted) {
                c.dsv->Release();
            }
            if (found) {
                return true;
            }
        }
    }
    // Auto: content-scored. Flat never wins; exact/half backbuffer size
    // dominates (shadow maps lose); clears break ties.
    auto cands = sortedSnapshot(mutex, candidates, true);
    ID3D11DepthStencilView* winner = nullptr;
    double bestScore = -1.0;
    unsigned bestW = 0, bestH = 0;
    DXGI_FORMAT bestFmt = DXGI_FORMAT_UNKNOWN;
    for (auto& c : cands) {
        if (!ensureCopy(device, context, c.dsv)) {
            continue;
        }
        double mn, mx, mean;
        if (!measureCopy(device, context, copyTex, copySrvFmt, copyW, copyH, staging, stagingFmt, stagingW, stagingH, mn, mx, mean)) {
            continue;
        }
        const double spread = mx - mn;
        const bool sized = (c.w == bw && c.h == bh) || (c.w * 2 == bw && c.h * 2 == bh);
        Logger->debug("lutdepth: candidate {}x{} fmt={} clears={} binds={} spread={:.4f} mean={:.4f}", c.w, c.h, (int)c.fmt, c.clears, c.binds, spread, mean);
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = candidates.find(c.dsv);
            if (it != candidates.end()) {
                it->second.lastSpread = spread;
            }
        }
        if (spread < FLAT_SPREAD_EPS) {
            continue;
        }
        double score = spread;
        if (sized) {
            score += SIZE_BONUS;
        }
        if (c.clears > 0) {
            score += CLEAR_BONUS;
        }
        if (score > bestScore + 1e-9) {
            bestScore = score;
            winner = c.dsv;
            bestW = c.w;
            bestH = c.h;
            bestFmt = c.fmt;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (selected) {
            selected->Release();
            selected = nullptr;
        }
        if (winner) {
            winner->AddRef();
            selected = winner;
            decidedVersion = candidateVersion;
            Logger->debug("lutdepth: selected {}x{} fmt={} (score {:.4f})", bestW, bestH, (int)bestFmt, bestScore);
        } else {
            decidedVersion = candidateVersion;
        }
    }
    for (auto& c : cands) {
        c.dsv->Release();
    }
    return winner != nullptr;
}

void LutDepth::refreshCpu(ID3D11Device* device, ID3D11DeviceContext* context, float nearZ, float farZ, bool linearize) {
    if (!copyTex || copyW == 0 || copyH == 0) {
        return;
    }
    DXGI_FORMAT wantFmt = DXGI_FORMAT_UNKNOWN;
    if (copySrvFmt == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) {
        wantFmt = DXGI_FORMAT_R24G8_TYPELESS;
    } else if (copySrvFmt == DXGI_FORMAT_R32_FLOAT) {
        wantFmt = DXGI_FORMAT_R32_TYPELESS;
    } else if (copySrvFmt == DXGI_FORMAT_R16_UNORM) {
        wantFmt = DXGI_FORMAT_R16_TYPELESS;
    } else {
        return;
    }
    if (!staging || stagingFmt != wantFmt || stagingW != copyW || stagingH != copyH) {
        if (staging) {
            staging->Release();
            staging = nullptr;
        }
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = copyW;
        sd.Height = copyH;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.Format = wantFmt;
        if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging))) {
            return;
        }
        stagingFmt = wantFmt;
        stagingW = copyW;
        stagingH = copyH;
    }
    context->CopyResource(staging, copyTex);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
        return;
    }
    auto out = std::make_shared<CpuDepth>();
    out->w = copyW;
    out->h = copyH;
    out->nearZ = nearZ;
    out->farZ = farZ;
    out->linearize = linearize;
    out->v.resize((size_t)copyW * copyH);
    const unsigned char* base = static_cast<const unsigned char*>(mapped.pData);
    for (unsigned y = 0; y < copyH; ++y) {
        const unsigned char* row = base + (size_t)y * mapped.RowPitch;
        for (unsigned x = 0; x < copyW; ++x) {
            double v = 1.0;
            if (copySrvFmt == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) {
                const unsigned char* px = row + (size_t)x * 4;
                v = ((unsigned)px[0] | ((unsigned)px[1] << 8) | ((unsigned)px[2] << 16)) / 16777215.0;
            } else if (copySrvFmt == DXGI_FORMAT_R32_FLOAT) {
                v = *(const float*)(row + (size_t)x * 4);
            } else if (copySrvFmt == DXGI_FORMAT_R16_UNORM) {
                v = *(const unsigned short*)(row + (size_t)x * 2) / 65535.0;
            }
            if (v < 0.0 || v > 1.0 || v != v) {
                v = 1.0;
            }
            out->v[(size_t)y * copyW + x] = (float)v;
        }
    }
    context->Unmap(staging, 0);
    std::lock_guard<std::mutex> lock(mutex);
    cpu = out;
}

ID3D11ShaderResourceView* LutDepth::update(ID3D11Device* device, ID3D11DeviceContext* context, unsigned backW, unsigned backH, const std::string& sourceKey,
                                           int everyN, bool freeze, float nearZ, float farZ, bool linearize, bool needCopy) {
    if (!device || !context) {
        return nullptr;
    }
    ++currentFrame;
    // decide source at most every 5s, immediately on key change, or when stale.
    bool needDecide = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool fresh = false;
        if (selected) {
            auto it = candidates.find(selected);
            fresh = it != candidates.end() && currentFrame - it->second.lastSeenFrame < STALE_FRAMES;
        }
        if (fresh && lastDecideKey == sourceKey) {
        } else if (selected && lastDecideKey == sourceKey && decidedVersion == candidateVersion) {
            needDecide = (GetTickCount64() - lastDecideTick > DECIDE_INTERVAL_MS);
            if (!needDecide && !needCopy) {
                return copySrv;
            }
        } else {
            needDecide = true;
        }
    }
    if (needDecide && !decide(device, context, backW, backH, sourceKey)) {
        if (!needCopy) {
            return copySrv;
        }
    }
    if (!needCopy) {
        return copySrv;
    }
    if (everyN < 1) {
        everyN = 1;
    }
    if (!freeze && (tick++ % (unsigned)everyN) == 0) {
        ID3D11DepthStencilView* dsv = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            dsv = selected;
        }
        if (dsv && ensureCopy(device, context, dsv)) {
            refreshCpu(device, context, nearZ, farZ, linearize);
        }
    }
    return copySrv;
}
