#ifndef SAS_SHADER_PASS_H
#define SAS_SHADER_PASS_H

// fullscreen post-process pass. compiles embedded HLSL, owns cb + sampler +
// ping-pong temps. caller saves/restores D3D state around run().
// caller also copies swapchain backbuffer into tempA before a multi-pass chain.

#include <d3d11.h>
#include <string>
#include <vector>

struct PassShader {
        std::string source;
        std::string entryPoint;
        std::string target;
};

class FullscreenPass {
    public:
        FullscreenPass() = default;
        ~FullscreenPass();

        bool initialize(ID3D11Device* device, const std::vector<PassShader>& shaders, unsigned cbSize, const std::string& pixelEntry = "PS");
        void shutdown();

        // per-frame draw. binds inputs[0..2] to t0..t2, unbinding all 3 after.
        bool run(ID3D11DeviceContext* context, const void* cbData, unsigned cbSize, ID3D11ShaderResourceView* const* inputs, unsigned inputCount,
                 ID3D11RenderTargetView* outputRtv, unsigned width, unsigned height, DXGI_FORMAT format);

        void onResize();

        // ensure ping-pong temp textures exist at the given dimensions/format.
        bool ensureTempsIfNeeded(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format);

        ID3D11Texture2D* tempTextureA() const {
            return tempA_;
        }
        ID3D11Texture2D* tempTextureB() const {
            return tempB_;
        }
        ID3D11ShaderResourceView* tempShaderResourceViewA() const {
            return tempSrvA_;
        }
        ID3D11ShaderResourceView* tempShaderResourceViewB() const {
            return tempSrvB_;
        }
        ID3D11RenderTargetView* tempRenderTargetViewA() const {
            return tempRtvA_;
        }
        ID3D11RenderTargetView* tempRenderTargetViewB() const {
            return tempRtvB_;
        }
        ID3D11VertexShader* vertexShader() const {
            return vertexShader_;
        }
        ID3D11PixelShader* pixelShader() const {
            return pixelShader_;
        }
        ID3D11SamplerState* sampler() const {
            return samplerState_;
        }
        ID3D11Buffer* constantBuffer() const {
            return constantBuffer_;
        }

    private:
        std::vector<PassShader> shaders_;

        ID3D11VertexShader* vertexShader_ = nullptr;
        ID3D11PixelShader* pixelShader_ = nullptr;
        ID3D11SamplerState* samplerState_ = nullptr;
        ID3D11Buffer* constantBuffer_ = nullptr;

        ID3D11Texture2D* tempA_ = nullptr;
        ID3D11Texture2D* tempB_ = nullptr;
        ID3D11ShaderResourceView* tempSrvA_ = nullptr;
        ID3D11ShaderResourceView* tempSrvB_ = nullptr;
        ID3D11RenderTargetView* tempRtvA_ = nullptr;
        ID3D11RenderTargetView* tempRtvB_ = nullptr;
        unsigned tempW_ = 0;
        unsigned tempH_ = 0;
        DXGI_FORMAT tempFmt_ = DXGI_FORMAT_UNKNOWN;
        std::string pixelEntry_ = "PS";

        bool compileShaders(ID3D11Device* device);
        bool createSampler(ID3D11Device* device);
        bool createConstantBuffer(ID3D11Device* device, unsigned cbSize);
        bool ensureTemps(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format);
        void releaseTemps();
        void releaseDeviceObjects();
};

#endif // SAS_FULLSCREEN_PASS_H
