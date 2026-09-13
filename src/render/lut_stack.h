#ifndef SAS_LUT_STACK_H
#define SAS_LUT_STACK_H

// thin wrapper around LutEffect

#include "lut_effect.h"

#include <d3d11.h>
#include <dxgi.h>
#include <memory>
#include <string>
#include <vector>

inline constexpr int LUT_MAX_LAYERS = 8;

class LutStack {
    public:
        LutStack();
        ~LutStack();

        void rescan();
        const std::vector<LutCatalogEntry>& catalog() const;

        void addLayer(const std::string& ref);
        void removeLayer(size_t index);

        bool apply(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* backbufferRtv);

        void onResize();
        void shutdown();
        void removeDepthHook();

        LutDepth& depth();
        bool& freezeDepth();

        std::vector<LutCpuLayer> snapshot();
        void renderDepthView(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depthSrv, ID3D11RenderTargetView* dst, unsigned w,
                             unsigned h, bool linearize, float nearZ, float farZ, bool invert, bool heat);

    private:
        LutEffect effect_;
};

#endif // SAS_LUT_STACK_H
