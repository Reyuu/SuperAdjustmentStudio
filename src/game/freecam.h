#ifndef SAS_FREECAM_H
#define SAS_FREECAM_H

#include <string>
#include <atomic>
#include <mutex>

#include "settings.h"

#include <LESDK/Includes.hpp>
#include <LESDK/Common/Math.hpp>

enum CameraDragState {
    CAMERA_DRAG_INACTIVE,
    CAMERA_DRAG_ORBITING,
    CAMERA_DRAG_PANNING,
    CAMERA_DRAG_VERTICAL_PANNING
};

struct CameraOffsets {
        FVector position;
        FRotator rotation;
        float roll = 0;
        float fov = 75;
};

struct FreeCamProperties {
        float moveSpeed = 1.0f;
};

struct FreecamPostProcessProperties {
        float bloomThreshold = 1.0f;
        float bloomScale = 1.0f;
        float dofDistance = 1.0f;
        float dofFStop = 1.0f;
        float desaturation = 1.0f;
};

class Freecam {
    public:
        void setOffsets(const CameraOffsets& offsets);
        CameraOffsets getOffsets() const {
            return offsets;
        };

        std::atomic<CameraDragState>& cameraDragState() {
            return cameraDragStateEnum;
        };
        std::atomic<bool>& freecamWanted() {
            return freecamWantedState;
        };
        std::atomic<bool>& isCameraDragActive() {
            return isCameraDragActiveBool;
        };

        void detour();
        void preDetour();

        void renderUi();

        void resetOffsets() {
            std::lock_guard<std::mutex> lock(freecamMutex);
            offsets = defaultOffsets;
        };
        void setFreecamEnabled(bool enabled);
        void applyFreecamEnabled(bool enabled);
        void applyFreecamLook(bool neutral = false);
        void syncFreecamHides(bool neutral = false);
        const char* dragModeSuffix();
        bool isFreecamActive();

        void moveFreecam(int dx, int dy, CameraDragState& state);

        void resetFreecamToPlayer();
        void resetFreecamState();

        void assertFreecamState();
        void assertFreecamCache();

        void assertFreecamPOV(UObject* context, UFunction* function, void* params);
        void overrideFillCameraCache(UObject* context, UFunction* function, void* params);
        void observeFreecamPOV(UObject* context, UFunction* function, void* params);
        void cacheLivePP(const FPostProcessSettings& pp);
        void seedSlidersFromLive();
        FVector dofFocusPoint(float distance);
        void applyDofPP(FPostProcessSettings& pp, const SettingsOptions& options);
        void applyBloomPP(FPostProcessSettings& pp, const SettingsOptions& options);
        void applyColorPP(FPostProcessSettings& pp, const SettingsOptions& options);
        void observeModePostProcess(UObject* context, UFunction* function, void* params);
        bool preFreecamCameraHook(UObject* context, UFunction* function);
        void requestFreecamSeed();

        CameraOffsets offsets;
        FreeCamProperties properties;

    private:
        CameraOffsets defaultOffsets;
        std::atomic<bool> isCameraDragActiveBool{false};
        std::atomic<bool> freecamWantedState{false};
        std::atomic<CameraDragState> cameraDragStateEnum{CAMERA_DRAG_INACTIVE};
        std::mutex freecamMutex;

        FVector viewForward;

        FreecamPostProcessProperties ppLive;

        bool freecamHasPOV{false};
        bool freecamNeedSeed{false};

        ASFXPlayerCamera* freecamCamera{nullptr};
        UFunction* freecamCamFunction{nullptr};
        UFunction* freecamFillFunction{nullptr};
        USFXCameraMode* freecamCameraMode{nullptr};
        USFXCameraMode* freecamPreviousCameraMode{nullptr};
};

#endif