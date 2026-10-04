#ifndef SAS_CUSTOM_SHADER_EFFECT_H
#define SAS_CUSTOM_SHADER_EFFECT_H

#include "custom_shader.h"
#include "custom_shader_catalog.h"
#include "post_chain.h"

#include <d3d11.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CustomShaderEffect : public IEffect {
    public:
        CustomShaderEffect() = default;
        ~CustomShaderEffect() override;
        CustomShaderEffect(const CustomShaderEffect&) = delete;
        CustomShaderEffect& operator=(const CustomShaderEffect&) = delete;

        bool isEnabled() const override;
        void applyGpu(ID3D11Device* device, ID3D11DeviceContext* context, PostFrame& frame) override;
        void applyCpu(unsigned char* rgba, int w, int h) const override;
        void renderUI() override; // custom_shader_panel.cpp
        void onResize() override;
        void shutdown() override;

        // screenshot path: runs the slots over an already decoded image
        bool bakeGpu(ID3D11Device* device, ID3D11DeviceContext* context, unsigned char* rgba, int w, int h, unsigned width, unsigned height);

        // panel helpers
        std::vector<CustomShaderCatalogEntry> catalog();
        void rescan();
        CustomShaderSlot* slotFor(const std::filesystem::path& absPath);
        std::string errorFor(const std::filesystem::path& absPath) const;
        void recompileAll(ID3D11Device* device);

    private:
        struct ActiveSlot {
                CustomShaderSlot* slot = nullptr;
                const CustomShaderOptions* options = nullptr;
        };

        CustomShaderSlot* ensureSlot(ID3D11Device* device, const std::filesystem::path& absPath);
        std::vector<ActiveSlot> collectActive(ID3D11Device* device);
        void pollReload(ID3D11Device* device);
        double elapsedTime() const; // seconds since the first frame, what gTime carries

        mutable std::mutex mutex_;
        std::map<std::string, std::unique_ptr<CustomShaderSlot>> slots_;
        std::map<std::string, std::vector<unsigned char>> constants_;
        std::vector<CustomShaderCatalogEntry> catalogEntries_;
        bool catalogScanned_ = false;
        bool examplesWritten_ = false;
        double lastPollTime_ = -1.0;
        double startTime_ = -1.0;
};

#endif // SAS_CUSTOM_SHADER_EFFECT_H