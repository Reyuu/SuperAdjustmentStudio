#ifndef SAS_CUSTOM_SHADER_H
#define SAS_CUSTOM_SHADER_H

//   float4 gParams[SAS_NUM_FLOATS];   // slider drag input toggle checkbox radio combo listbox
//   int4   gIntParams[SAS_NUM_INTS];  // dragInt inputInt (optional: values arrive as floats when absent)
//   float4 gColors[SAS_NUM_COLORS];   // color
//   float  gTime;                     // written here
//   float2 gResolution;               // written here
//   int    gFlags;                    // written here: bit0 hasDepth, bit1 isLastPass, bits 8-15 passIndex
// `int gPad` is accepted as the trailing flags word

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>

#include "pass.h"
#include "settings.h"

enum class CustomShaderParamKind {
    Slider,
    Drag,
    Input,
    DragInt,
    InputInt,
    Toggle,
    Radio,
    Combo,
    ListBox,
    Color
};

struct CustomShaderParam {
        CustomShaderParamKind kind = CustomShaderParamKind::Slider;
        std::string name;
        std::vector<std::string> items; // radio / combo / listbox labels
        float min = 0.0f;
        float max = 1.0f;
        float def = 0.0f;
        float step = 0.0f; // 0 means "pick a sensible step from the range"
        int intDef = 0;
        std::array<float, 4> colorDef = {0.0f, 0.0f, 0.0f, 1.0f};
        bool hasAlpha = false;
        int floatIndex = -1; // slot inside gParams
        int intIndex = -1;   // slot inside gIntParams
        int colorIndex = -1; // slot inside gColors

        bool usesFloatSlot() const {
            return kind != CustomShaderParamKind::DragInt && kind != CustomShaderParamKind::InputInt && kind != CustomShaderParamKind::Color;
        }
        bool usesIntSlot() const {
            return kind == CustomShaderParamKind::DragInt || kind == CustomShaderParamKind::InputInt;
        }
};

// scans source for `// sas: <kind>(args)`. assigns the per-family indices.
// any malformed, unknown or out-of-range annotation fails the whole parse, so
// indices can never shift silently.
bool parseCustomShaderAnnotations(const std::string& source, std::vector<CustomShaderParam>& outParams, int& outFloatCount, int& outIntCount,
                                  int& outColorCount, std::string& outError);

class CustomShaderSlot {
    public:
        CustomShaderSlot() = default;
        ~CustomShaderSlot();
        CustomShaderSlot(const CustomShaderSlot&) = delete;
        CustomShaderSlot& operator=(const CustomShaderSlot&) = delete;

        bool load(ID3D11Device* device, const std::filesystem::path& absPath, std::string& error);

        // true when the source or one of its includes changed on disk
        bool needsRecompile() const;

        bool isReady() const {
            return ready_;
        }
        const std::string& lastError() const {
            return lastError_;
        }
        const std::vector<CustomShaderParam>& params() const {
            return params_;
        }
        const std::filesystem::path& path() const {
            return path_;
        }
        unsigned cbSize() const {
            return cbSize_;
        }
        int annotationFloatCount() const {
            return floatCount_;
        }
        int annotationIntCount() const {
            return intCount_;
        }
        int annotationColorCount() const {
            return colorCount_;
        }

        // writes the whole b0 buffer: widget values by name, then gTime,
        // gResolution and gFlags. dst must be at least cbSize() bytes.
        void packConstants(void* dst, const CustomShaderOptions& values, float time, unsigned w, unsigned h, unsigned flags) const;

        // binds t0..t2, the constant buffer and s0, then draws the triangle.
        bool draw(ID3D11DeviceContext* context, const void* cbData, ID3D11ShaderResourceView* const* inputs, unsigned inputCount,
                  ID3D11RenderTargetView* target, unsigned width, unsigned height);

        void shutdown();

    private:
        // one watched file: the slot source plus every include it resolved
        using Dependency = std::pair<std::filesystem::path, std::filesystem::file_time_type>;

        struct Layout {
                int paramsOffset = 0;
                int intParamsOffset = -1;
                int colorsOffset = -1;
                int timeOffset = -1;
                int resolutionOffset = -1;
                int flagsOffset = 0;
                unsigned size = 0;
        };

        bool compile(ID3D11Device* device, const std::string& source, std::string& error);
        static bool parseConstantBuffer(const std::string& source, int floatSlots, int intSlots, int colorSlots, Layout& outLayout, std::string& error);
        static bool inspectConstantBuffer(const std::string& source, bool& outHasIntParams, bool& outHasColors, std::string& error);

        FullscreenPass pass_;
        std::filesystem::path path_;
        std::vector<CustomShaderParam> params_;
        std::vector<Dependency> dependencies_;
        Layout layout_;
        unsigned cbSize_ = 0;
        int floatCount_ = 0;
        int intCount_ = 0;
        int colorCount_ = 0;
        std::string lastError_;
        bool ready_ = false;
};

// resolves #include targets through custom_shader_catalog and records each one
// so the slot can watch them for changes
class CustomShaderIncludeHandler : public ID3DInclude {
    public:
        CustomShaderIncludeHandler(const std::filesystem::path& mainFile,
                                   std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>& dependencies, std::string& error);
        ~CustomShaderIncludeHandler();

        HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData, LPCVOID* data, UINT* dataSize) override;
        HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override;

    private:
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>& dependencies_;
        std::string& error_;
        std::vector<std::filesystem::path> stack_; // bottom = the slot's own file
};

#endif // SAS_CUSTOM_SHADER_H