#include "lut_stack.h"
#include "lut_effect.h"
#include <d3d11.h>
#include <dxgi.h>

LutStack::LutStack() = default;
LutStack::~LutStack() = default;

void LutStack::rescan() {
    effect_.rescan();
}

const std::vector<LutCatalogEntry>& LutStack::catalog() const {
    return effect_.catalog();
}

void LutStack::addLayer(const std::string& ref) {
    effect_.addLayer(ref);
}

void LutStack::removeLayer(size_t index) {
    effect_.removeLayer(index);
}

bool LutStack::apply(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv) {
    return effect_.apply(swapChain, device, context, backbufferRtv);
}

void LutStack::onResize() {
    effect_.onResize();
}

void LutStack::shutdown() {
    effect_.shutdown();
}

void LutStack::removeDepthHook() {
    effect_.removeDepthHook();
}

LutDepth& LutStack::depth() {
    return effect_.depth();
}

bool& LutStack::freezeDepth() {
    return effect_.freezeDepth();
}

std::vector<LutCpuLayer> LutStack::snapshot() {
    return effect_.snapshot();
}

void LutStack::renderDepthView(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depthSrv, ID3D11RenderTargetView* dst, unsigned w,
                               unsigned h, bool linearize, float nearZ, float farZ, bool invert, bool heat) {
    effect_.renderDepthView(device, context, depthSrv, dst, w, h, linearize, nearZ, farZ, invert, heat);
}
