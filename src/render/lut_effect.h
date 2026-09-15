#ifndef SAS_LUT_EFFECT_H
#define SAS_LUT_EFFECT_H

// LUT post-process effect. owns FullscreenPass + LUT texture cache + HLSL.
// first node of the PostChain. borrows the chain-owned LutDepth provider
// for the depth gate (freeze flag stays here, next to its UI).

#include "lut_catalog.h"
#include "lut_cpu.h"
#include "lut_depth.h"
#include "pass.h"
#include "post_chain.h"

#include <d3d11.h>
#include <dxgi.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline constexpr int LUT_MAX_LAYERS = 8;

// CB layout — mirrors HLSL cbuffer LutCB, field for field.
struct LutEffectConstants {
        int mode; // 0 hald, 1 strip, 2 mlut
        int level;
        int tilesPerRow;
        int padR1;
        float blend;
        float chroma;
        float luma;
        int padA;
        int tileSize;
        int tileAmount;
        int lutAmount;
        int selector;
        int useDepth;
        float focus;
        float range;
        float blendMin;
        float blendMax;
        int preview;
        int linMode;
        int heat;
        float nearZ;
        float farZ;
        int invert;
        int padD;
};

static_assert(sizeof(LutEffectConstants) == 96, "LutEffectConstants must match LutCB (6x16B rows)");

class LutEffect : public IEffect {
    public:
        LutEffect(LutDepth& depthProvider);
        ~LutEffect() override;

        bool initialize(ID3D11Device* device);
        void shutdown() override;
        void onResize() override;

        void rescan() const;
        const std::vector<LutCatalogEntry>& catalog() const;

        void addLayer(const std::string& ref);
        void removeLayer(size_t index);

        bool isEnabled() const override;
        void applyGpu(ID3D11Device* device, ID3D11DeviceContext* context, PostFrame& frame) override;
        void applyCpu(unsigned char* rgba, int w, int h) const override;
        void renderUI() override;

        LutDepth& depth();
        bool& freezeDepth();

        // CPU snapshot for screenshot baking.
        std::vector<LutCpuLayer> snapshot() const;

        // standalone depth texture render (when lutDepthShowTexture is on).
        void renderDepthView(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depthSrv, ID3D11RenderTargetView* dst, unsigned w,
                             unsigned h, bool linearize, float nearZ, float farZ, bool invert, bool heat);

    private:
        struct LutTexture {
                ID3D11Texture2D* texture = nullptr;
                ID3D11ShaderResourceView* srv = nullptr;
                int width = 0;
                int height = 0;
                std::shared_ptr<std::vector<unsigned char>> pixels;
        };

        bool ensureShaders(ID3D11Device* device);
        void ensureCatalog() const;
        LutEffect::LutTexture* ensureTexture(ID3D11Device* device, const std::string& absPath);
        bool resolveCatalog(const LutRef& ref, const std::filesystem::path& dir, LutCatalogEntry& out, std::string& absPath) const;

        static const char* lutHLSL();

        FullscreenPass pass_;
        ID3D11PixelShader* depthViewPixelShader_ = nullptr;

        mutable std::mutex mutex_;
        mutable std::vector<LutCatalogEntry> catalogEntries_;
        mutable bool catalogScanned_ = false;
        mutable std::map<std::string, LutTexture> textures_;

        LutDepth& depthProvider_;
        bool freezeDepthState_ = false;
};

#endif // SAS_LUT_EFFECT_H
