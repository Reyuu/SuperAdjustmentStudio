#ifndef SAS_LUT_DEPTH_H
#define SAS_LUT_DEPTH_H

#include <d3d11.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct CpuDepth {
        unsigned w = 0;
        unsigned h = 0;
        std::vector<float> v; // row-major raw depth 0..1
        float nearZ = 1.0f;
        float farZ = 100.0f;
        bool linearize = true;
};

struct DepthSourceInfo {
        std::string key; // "WxH:F@k"
        std::string label;
};

class LutDepth {
    public:
        LutDepth() = default;
        ~LutDepth();

        void setEnabled(bool enabled);
        void installHook(ID3D11DeviceContext* context);
        void removeHook();
        void shutdown();

        // per-frame (render thread)
        // Catalogs + selects always
        // copies the selected source and refreshes the CPU snapshot only when needCopy
        ID3D11ShaderResourceView* update(ID3D11Device* device, ID3D11DeviceContext* context, unsigned backW, unsigned backH, const std::string& sourceKey,
                                         int everyN, bool freeze, float nearZ, float farZ, bool linearize, bool needCopy);
        void onResize();

        // frozen UI list, refresh explicitly, never live.
        std::vector<DepthSourceInfo> sourceList();

        // latest CPU snapshot for screenshot baking (worker thread safe).
        std::shared_ptr<const CpuDepth> cpuSnapshot() const;

    private:
        struct Candidate {
                ID3D11DepthStencilView* dsv = nullptr;
                unsigned w = 0;
                unsigned h = 0;
                DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
                unsigned long long lastSeenFrame = 0;
                unsigned clears = 0;
                unsigned binds = 0;
                double lastSpread = -1.0;
        };

        void cacheDsv(ID3D11DepthStencilView* dsv, bool cleared);
        static std::vector<Candidate> sortedSnapshot(std::mutex& mutex, std::map<ID3D11DepthStencilView*, Candidate>& map, bool addRef);
        bool decide(ID3D11Device* device, ID3D11DeviceContext* context, unsigned bw, unsigned bh, const std::string& sourceKey);
        bool ensureCopy(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv);
        void refreshCpu(ID3D11Device* device, ID3D11DeviceContext* context, float nearZ, float farZ, bool linearize);

        friend void lutDepthHookCache(LutDepth* owner, ID3D11DepthStencilView* dsv, bool cleared);

        mutable std::mutex mutex;
        std::map<ID3D11DepthStencilView*, Candidate> candidates;
        ID3D11DepthStencilView* selected = nullptr;
        unsigned long long currentFrame = 0;
        unsigned long long candidateVersion = 0;
        unsigned long long decidedVersion = 0;
        unsigned long long lastDecideTick = 0;
        std::string lastDecideKey;

        ID3D11Texture2D* copyTex = nullptr;
        ID3D11ShaderResourceView* copySrv = nullptr;
        DXGI_FORMAT copySrvFmt = DXGI_FORMAT_UNKNOWN;
        unsigned copyW = 0;
        unsigned copyH = 0;

        ID3D11Texture2D* staging = nullptr;
        DXGI_FORMAT stagingFmt = DXGI_FORMAT_UNKNOWN;
        unsigned stagingW = 0;
        unsigned stagingH = 0;
        std::shared_ptr<CpuDepth> cpu;

        bool hookInstalled = false;
        bool hookFailed = false;
        void* hookTargets[3] = {};
        unsigned tick = 0;
};

#endif // SAS_LUT_DEPTH_H
