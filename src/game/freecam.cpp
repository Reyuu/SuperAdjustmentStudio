#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"
#include "freecam.h"
#include <algorithm>
#include "application.h"
#include "logger.h"
#include "util.h"
#include "settings.h"
#include <imgui.h>
#include "IconsFontAwesome6.h"
#include <LESDK/Includes.LE2.hpp>
#include <LESDK/Common/Math.hpp>

#define SAS_PP_SET(FLAG, FIELD, VALUE) \
    do {                               \
        FLAG = 1;                      \
        FIELD = (VALUE);               \
    } while (0)

static float DragPxToRad(int px) {
    return (float)px * SETTINGS_FREECAM_LOOK_DEG_PER_PX * DegreesToRadians;
}

static ABioPlayerController* findLocalBioPC() {
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    ABioPlayerController* fallback = nullptr;
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* obj = UObject::GObjObjects->GetData()[i];
        if (!obj || !obj->IsA(ABioPlayerController::StaticClass())) {
            continue;
        }

        std::string name = FStringToString(obj->GetName());
        if (name.rfind("Default__", 0) == 0) {
            continue;
        }

        ABioPlayerController* pc = static_cast<ABioPlayerController*>(obj);
        if (pc->IsLocalPlayerController()) {
            return pc;
        }
        if (!fallback) {
            fallback = pc;
        }
    }
    return fallback;
}

static bool getCurrentLiveView(FVector& outLocation, FRotator& outRotation, ASFXPlayerCamera*& outCamera) {
    ABioPlayerController* localPC = findLocalBioPC();
    if (!localPC) {
        return false;
    }
    ASFXPlayerCamera* playerCamera = nullptr;
    if (localPC->PlayerCamera && localPC->PlayerCamera->IsA(ASFXPlayerCamera::StaticClass())) {
        playerCamera = static_cast<ASFXPlayerCamera*>(localPC->PlayerCamera);
    }
    if (!playerCamera) {
        return false;
    }
    playerCamera->GetCameraViewPoint(&outLocation, &outRotation);
    outCamera = playerCamera;
    return true;
}

static USFXGameModePhoto* resolvePhotoGameMode() {
    ABioPlayerController* localPC = findLocalBioPC();
    if (!localPC) {
        return nullptr;
    }
    USFXGameModeManager* manager = localPC->GameModeManager2;
    USFXGameModePhoto* photoGameMode = manager ? manager->GetPhotoMode() : nullptr;
    if (photoGameMode && isLiveObject(photoGameMode)) {
        return photoGameMode;
    }
    return nullptr;
}

static ULocalPlayer* resolveLocalPlayer() {
    ABioPlayerController* localPC = findLocalBioPC();
    if (!localPC || !localPC->Player->IsA(ULocalPlayer::StaticClass())) {
        return nullptr;
    }
    return static_cast<ULocalPlayer*>(localPC->Player);
}

void Freecam::setFreecamEnabled(bool enabled) {
    freecamWantedState.store(enabled);
    Application::instance().engine().postGameThreadTask([this, enabled]() {
        applyFreecamEnabled(enabled);
    });
}

bool Freecam::isFreecamActive() {
    return freecamWantedState.load() && freecamHasPOV;
}

void Freecam::applyFreecamEnabled(bool enabled) {
    if (enabled) {
        FVector loc{0.0f, 0.0f, 0.0f};
        FRotator rot{0, 0, 0};
        ASFXPlayerCamera* cam = nullptr;
        if (!getCurrentLiveView(loc, rot, cam)) {
            Logger->debug("freecam: no live camera to detach from");
            freecamWantedState.store(false);
            return;
        }
        if (!freecamCamFunction) {
            freecamCamFunction = UObject::FindObject<UFunction>(L"Function SFXGame.SFXPlayerCamera.UpdateCameraManager");
        }

        {
            std::lock_guard<std::mutex> lock(freecamMutex);
            offsets.position = loc;
            offsets.rotation = rot;
            offsets.roll = 0;
            freecamHasPOV = true;
        }
        freecamCamera = cam;

        ABioPlayerController* localPC = findLocalBioPC();
        if (localPC) {
            USFXGameModePhoto* photoGameMode = resolvePhotoGameMode();
            if (photoGameMode) {
                USFXCameraMode_PhotoFree* target = photoGameMode->PhotoCamFree;
                if (photoGameMode && isLiveObject(photoGameMode) && freecamCamera->CurrentCameraMode != target) {
                    freecamPreviousCameraMode = freecamCamera->CurrentCameraMode;
                    target->Initialize(localPC);
                    cam->FreeCam = target;
                    cam->bFreeCamActive = true;
                    localPC->bPhotoModeCameraUnlocked = true;
                    cam->SwitchTo(target);
                }
            }
        }

        applyFreecamLook(false);
        if (USFXCameraMode* mode = cam->CurrentCameraMode) {
            mode->bCollisionEnabled = false;
        }
        if (Application::instance().settings().options.freecamAdjust) {
            freecamNeedSeed = true;
        }
    } else {
        applyFreecamLook(true);
        {
            std::lock_guard<std::mutex> lock(freecamMutex);
            freecamHasPOV = false;
        }

        ABioPlayerController* localPC = findLocalBioPC();
        if (localPC) {
            if (freecamCamera) {
                freecamCamera->bFreeCamActive = false;
                localPC->bPhotoModeCameraUnlocked = false;
                freecamCamera->PickCameraMode(0.0f);
                USFXCameraMode* prev = nullptr;
                if (freecamPreviousCameraMode && isLiveObject(freecamPreviousCameraMode)) {
                    prev = freecamPreviousCameraMode;
                }
                if (prev && freecamCamera->CurrentCameraMode != prev) {
                    freecamCamera->SwitchTo(prev);
                }
            }
        }
        if (freecamCamera) {
            USFXCameraMode* current = freecamCamera->CurrentCameraMode;
            if (current) {
                current->bCollisionEnabled = true;
            }
        }
        freecamPreviousCameraMode = nullptr;
        freecamCamera = nullptr;
        isCameraDragActiveBool.store(false);
        cameraDragStateEnum.store(CAMERA_DRAG_INACTIVE);
    }
}

void Freecam::resetFreecamToPlayer() {
    FVector loc{0.0f, 0.0f, 0.0f};
    FRotator rot{0, 0, 0};
    ASFXPlayerCamera* cam = nullptr;
    if (!getCurrentLiveView(loc, rot, cam)) {
        return;
    }

    std::lock_guard<std::mutex> lock(freecamMutex);
    offsets.position = loc;
    offsets.rotation = rot;
    offsets.roll = 0;
}

void Freecam::resetFreecamState() {
    freecamWantedState.store(false);
    {
        std::lock_guard<std::mutex> lock(freecamMutex);
        freecamHasPOV = false;
    }
    freecamCamera = nullptr;
    ULocalPlayer* localPlayer = resolveLocalPlayer();
    if (localPlayer) {
        localPlayer->ClearPostProcessSettingsOverride(0.0f);
    }

    syncFreecamHides();
    isCameraDragActiveBool.store(false);
    cameraDragStateEnum.store(CAMERA_DRAG_INACTIVE);
}

void Freecam::assertFreecamCache() {
    if (!freecamWantedState.load() || !freecamHasPOV || !freecamCamera) {
        return;
    }

    SettingOptions& options = Application::instance().settings().options;
    std::lock_guard<std::mutex> lock(freecamMutex);

    FRotator rot = offsets.rotation;
    rot.Roll = DegreesToUnrealRotationUnits(std::clamp(options.freecamRoll, SETTINGS_FREECAM_ROLL_MIN, SETTINGS_FREECAM_ROLL_MAX));
    freecamCamera->CameraCache.POV.Location = offsets.position;
    freecamCamera->CameraCache.POV.Rotation = rot;
    freecamCamera->CameraCache.POV.FOV = std::clamp(options.freecamFOV, SETTINGS_FREECAM_FOV_MIN, SETTINGS_FREECAM_FOV_MAX);
}

bool Freecam::preFreecamCameraHook(UObject* context, UFunction* function) {
    if (!freecamWantedState.load() || !freecamHasPOV || !freecamCamera || !context || !function) {
        return false;
    }
    if (context == freecamCamera) {
        return true;
    }
    const USFXCameraMode* current = freecamCamera->CurrentCameraMode;
    return current && context == current;
}

void Freecam::requestFreecamSeed() {
    freecamNeedSeed = true;
}

void Freecam::applyFreecamLook(bool neutral) {
    syncFreecamHides(neutral);
    if (neutral) {
        if (ULocalPlayer* localPlayer = resolveLocalPlayer()) {
            localPlayer->ClearPostProcessSettingsOverride(0.0f);
        }
    } else {
        assertFreecamCache();
    }
}

void Freecam::overrideFillCameraCache(UObject*, UFunction*, void*) {
}

void orbitLook(FRotator& rot, int dx, int dy, float rollDeg) {
    FMatrix basis = ViewBasisFromRotatorRoll(rot, DegreesToUnrealRotationUnits(rollDeg));
    FVector forward = basis.XPlane;
    FVector right = basis.YPlane;
    FVector up = basis.ZPlane;

    forward = RotateAboutUnit(forward, up, DragPxToRad(dx));
    right = RotateAboutUnit(right, up, DragPxToRad(dx));
    forward = RotateAboutUnit(forward, right, DragPxToRad(dy));

    rot.Yaw = RadiansToUnrealRotationUnits(atan2f(forward.Y, forward.X));
    rot.Pitch = RadiansToUnrealRotationUnits(std::clamp(asinf(std::clamp(forward.Z, -1.0f, 1.0f)), -MaxInPitchRadians, MaxInPitchRadians));
}

void flyPan(FVector& loc, const FRotator& rot, int dx, int dy, float speed) {
    FMatrix basis = ViewBasisFromRotatorRoll(rot, 0);
    FVector forward = basis.XPlane;
    FVector right = basis.YPlane;

    loc += (right * (float)dx + forward * (float)-dy) * (SETTINGS_FREECAM_UU_PER_PX * speed);
}

void liftPan(FVector& loc, int dy, float speed) {
    loc.Z += (float)-dy * (SETTINGS_FREECAM_UU_PER_PX * speed);
}

void Freecam::moveFreecam(int dx, int dy, CameraDragState& state) {
    if (!freecamWantedState.load() || !freecamHasPOV || !freecamCamera) {
        return;
    }
    SettingOptions options = Application::instance().settings().options;
    std::lock_guard<std::mutex> lock(freecamMutex);
    switch (state) {
        case CAMERA_DRAG_ORBITING: {
            orbitLook(offsets.rotation, dx, dy, std::clamp(options.freecamRoll, SETTINGS_FREECAM_ROLL_MIN, SETTINGS_FREECAM_ROLL_MAX));
            break;
        }
        case CAMERA_DRAG_PANNING: {
            flyPan(offsets.position, offsets.rotation, dx, dy, options.freecamMoveSpeed);
            break;
        }
        case CAMERA_DRAG_VERTICAL_PANNING: {
            liftPan(offsets.position, dy, options.freecamMoveSpeed);
            break;
        }
        default: {
            break;
        }
    }
}

FVector gray(float v) {
    return FVector{v, v, v};
}

void Freecam::cacheLivePP(const FPostProcessSettings& pp) {
    ppLive.bloomThreshold = pp.Bloom_Threshold;
    ppLive.bloomScale = pp.Bloom_Scale;
    ppLive.dofDistance = pp.DOF_FocusDistance;
    ppLive.dofFStop = pp.DOF_FStop;
    ppLive.desaturation = pp.Scene_Desaturation;
}

void Freecam::seedSlidersFromLive() {
    SettingOptions& options = Application::instance().settings().options;
    options.freecamBloomThreshold = ppLive.bloomThreshold;
    options.freecamBloomScale = ppLive.bloomScale;
    options.freecamDofDistance = ppLive.dofDistance;
    options.freecamDofFStop = ppLive.dofFStop;
    options.freecamSat = ppLive.desaturation;
    Application::instance().settings().markChanged();
    Logger->debug("freecam: sliders seeded from live scene PP");
}

FVector Freecam::dofFocusPoint(float distance) {
    std::lock_guard<std::mutex> lock(freecamMutex);
    return offsets.position + viewForward * distance;
}

void Freecam::applyDofPP(FPostProcessSettings& pp, const SettingOptions& options) {
    SAS_PP_SET(pp.bOverride_EnableDOF, pp.bEnableDOF, 1);
    SAS_PP_SET(pp.bOverride_DOF_FocusDistance, pp.DOF_FocusDistance, options.freecamDofDistance);
    SAS_PP_SET(pp.bOverride_DOF_FocusInnerRadius, pp.DOF_FocusInnerRadius, options.freecamDofInnerRadius);
    SAS_PP_SET(pp.bOverride_DOF_FocusPosition, pp.DOF_FocusPosition, dofFocusPoint(options.freecamDofDistance));
    SAS_PP_SET(pp.bOverride_DOF_FStop, pp.DOF_FStop, options.freecamDofFStop);
    SAS_PP_SET(pp.bOverride_DOF_MaxFarBlurAmount, pp.DOF_MaxFarBlurAmount, std::clamp(options.freecamDofIntensity / 4.0f, 0.0f, 1.0f));
}

void Freecam::applyBloomPP(FPostProcessSettings& pp, const SettingOptions& options) {
    SAS_PP_SET(pp.bOverride_EnableBloom, pp.bEnableBloom, 1);
    SAS_PP_SET(pp.bOverride_Bloom_Threshold, pp.Bloom_Threshold, options.freecamBloomThreshold);
    SAS_PP_SET(pp.bOverride_Bloom_Scale, pp.Bloom_Scale, options.freecamBloomScale);
}

void Freecam::applyColorPP(FPostProcessSettings& pp, const SettingOptions& options) {
    SAS_PP_SET(pp.bOverride_Scene_HighLights, pp.Scene_HighLights, gray(options.freecamContrast));
    SAS_PP_SET(pp.bOverride_Scene_MidTones, pp.Scene_MidTones, gray(options.freecamBright));
    SAS_PP_SET(pp.bOverride_Scene_Shadows, pp.Scene_Shadows, gray(std::clamp(2.0f - options.freecamContrast, 0.0f, 2.0f)));
    SAS_PP_SET(pp.bOverride_Scene_Desaturation, pp.Scene_Desaturation, std::clamp(1.0f - options.freecamSat, 0.0f, 1.0f));
}

void Freecam::observeModePostProcess(UObject* context, UFunction* function, void* params) {
    if (!freecamWantedState.load() || !freecamHasPOV || !context || !function || !params || !freecamCamera) {
        return;
    }
    if (!function->GetName().Equals(L"ModifyPostProcessSettings") || context != freecamCamera->CurrentCameraMode) {
        return;
    }

    auto* p = reinterpret_cast<USFXCameraMode_eventModifyPostProcessSettings_Parms*>(params);
    cacheLivePP(p->PPSettings);
    if (freecamNeedSeed) {
        freecamNeedSeed = false;
        seedSlidersFromLive();
    }

    SettingOptions options = Application::instance().settings().options;

    if (!options.isFreecamAdjustEnabled) {
        return;
    }
    if (options.isFreecamDofEnabled) {
        applyDofPP(p->PPSettings, options);
    }
    if (options.isFreecamBloomEnabled) {
        applyBloomPP(p->PPSettings, options);
    }
    if (options.isFreecamColorEnabled) {
        applyColorPP(p->PPSettings, options);
    }
}

void Freecam::syncFreecamHides(bool neutral) {
    USFXGameModePhoto* photoMode = resolvePhotoGameMode();
    if (!photoMode) {
        return;
    }

    SettingOptions options = Application::instance().settings().options;

    const bool others = !neutral && options.isFreecamHideOthersEnabled;
    const bool self = !neutral && options.isFreecamHideSelfEnabled;
    const bool npcs = !neutral && options.isFreecamHideNPCsEnabled;
    const bool party = !neutral && options.isFreecamHidePartyEnabled;
    const bool vehicle = !neutral && options.isFreecamHideVehicleEnabled;
    photoMode->SetPlayerHidden(self);
    photoMode->SetPartyHidden(party);
    photoMode->SetEnemiesHidden(others);
    photoMode->SetNPCsHidden(npcs);
    photoMode->SetVehicleHidden(vehicle);
}

const char* Freecam::dragModeSuffix() {
    if (!isCameraDragActive().load()) {
        return "";
    }
    switch ((CameraDragState)cameraDragState().load()) {
        case CAMERA_DRAG_ORBITING:
            return " - look (RMB)";
        case CAMERA_DRAG_PANNING:
            return " - pan (SHIFT+RMB)";
        case CAMERA_DRAG_VERTICAL_PANNING:
            return " - lift (SHIFT+LMB)";
        default:
            return "";
    }
}

void Freecam::renderUi() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_CAMERA " ") + t("ui.freecam")).c_str())) {
        return;
    }

    ImGui::Indent();
    auto& app = Application::instance();
    SettingOptions& options = app.settings().options;

    auto pushLook = [&app]() {
        app.engine().postGameThreadTask([]() {
            Application::instance().freecam().applyFreecamLook(false);
        });
    };

    auto touched = [&](bool push = true) {
        app.settings().markChanged();
        if (push) {
            pushLook();
        }
    };

    auto slider = [&](const char* label, float& value, float min, float max, bool push = true, bool invert = false) {
        float span = max - min;
        float t = std::clamp(invert ? (max - value) / span : (value - min) / span, 0.0f, 1.0f);
        if (ImGui::SliderFloat(label, &t, 0.0f, 1.0f, "%.3f")) {
            value = invert ? max - std::clamp(t, 0.0f, 1.0f) * span : min + std::clamp(t, 0.0f, 1.0f) * span;
            touched(push);
        }
    };
    auto rawSlider = [&](const char* label, float& value, float min, float max, bool push = true) {
        if (ImGui::SliderFloat(label, &value, min, max, "%.3f")) {
            value = std::clamp(value, min, max);
            touched(push);
        }
    };

    auto toggle = [&](const char* label, bool& value) {
        if (ImGui::Checkbox(label, &value)) {
            touched();
        }
    };

    bool enabled = freecamWantedState.load();
    if (ImGui::Checkbox((std::string(ICON_FA_VIDEO " ") + t("ui.freecam_table.enable")).c_str(), &enabled)) {
        setFreecamEnabled(enabled);
    }

    ImGui::PushItemWidth(-100);
    rawSlider((std::string(t("ui.freecam_table.move_speed")) + "##freecam_speed").c_str(), options.freecamMoveSpeed, SETTINGS_FREECAM_MOVE_SPEED_MIN,
              SETTINGS_FREECAM_MOVE_SPEED_MAX, false);
    rawSlider((std::string(t("ui.freecam_table.fov")) + "##freecam_fov").c_str(), options.freecamFOV, SETTINGS_FREECAM_FOV_MIN, SETTINGS_FREECAM_FOV_MAX,
              false);
    slider((std::string(t("ui.freecam_table.roll")) + "##freecam_roll").c_str(), options.freecamRoll, SETTINGS_FREECAM_ROLL_MIN, SETTINGS_FREECAM_ROLL_MAX,
           false);
    if (ImGui::Checkbox((std::string(ICON_FA_WAND_SPARKLES " ") + t("ui.freecam_table.photo_adjust") + "##freecam_adjust").c_str(),
                        &options.isFreecamAdjustEnabled)) {
        app.settings().markChanged();
        app.engine().postGameThreadTask([]() {
            Application::instance().freecam().requestFreecamSeed();
            Application::instance().freecam().applyFreecamLook(false);
        });
    }
    if (options.isFreecamAdjustEnabled) {
        ImGui::Indent();
        ImGui::SeparatorText(t("ui.freecam_table.lens"));
        toggle((std::string(t("ui.freecam_table.dof")) + "##freecam_dof").c_str(), options.isFreecamDofEnabled);
        if (options.isFreecamDofEnabled) {
            rawSlider((std::string(t("ui.freecam_table.focus_dist")) + "##freecam_dofdist").c_str(), options.freecamDofDistance,
                      SETTINGS_FREECAM_DOF_DISTANCE_MIN, SETTINGS_FREECAM_DOF_DISTANCE_MAX);
            rawSlider((std::string(t("ui.freecam_table.focus_inn_rad")) + "##freecam_dofinner").c_str(), options.freecamDofInnerRadius,
                      SETTINGS_FREECAM_DOF_INNER_RADIUS_MIN, SETTINGS_FREECAM_DOF_INNER_RADIUS_MAX);
            rawSlider((std::string(t("ui.freecam_table.fstop")) + "##freecam_doffstop").c_str(), options.freecamDofFStop, SETTINGS_FREECAM_DOF_FSTOP_MIN,
                      SETTINGS_FREECAM_DOF_FSTOP_MAX);
            slider((std::string(t("ui.freecam_table.dof_intensity")) + "##freecam_dofint").c_str(), options.freecamDofIntensity,
                   SETTINGS_FREECAM_DOF_INTENSITY_MIN, SETTINGS_FREECAM_DOF_INTENSITY_MAX);
        }
        ImGui::SeparatorText(t("ui.freecam_table.bloom"));
        toggle((std::string(t("ui.freecam_table.enable_bloom")) + "##freecam_bloomon").c_str(), options.isFreecamBloomEnabled);
        slider((std::string(t("ui.freecam_table.bloom_threshold")) + "##freecam_bloomthr").c_str(), options.freecamBloomThreshold,
               SETTINGS_FREECAM_BLOOM_THRESHOLD_MIN, SETTINGS_FREECAM_BLOOM_THRESHOLD_MAX);
        slider((std::string(t("ui.freecam_table.bloom_intensity")) + "##freecam_bloomint").c_str(), options.freecamBloomScale, SETTINGS_FREECAM_BLOOM_SCALE_MIN,
               SETTINGS_FREECAM_BLOOM_SCALE_MAX);
        ImGui::SeparatorText(t("ui.freecam_table.color"));
        toggle((std::string(t("ui.freecam_table.enable_color")) + "##freecam_coloron").c_str(), options.isFreecamColorEnabled);
        slider((std::string(t("ui.freecam_table.contrast")) + "##freecam_contrast").c_str(), options.freecamContrast, SETTINGS_FREECAM_CONTRAST_MIN,
               SETTINGS_FREECAM_CONTRAST_MAX);
        slider((std::string(t("ui.freecam_table.brightness")) + "##freecam_bright").c_str(), options.freecamBright, SETTINGS_FREECAM_COLOR_MIN,
               SETTINGS_FREECAM_COLOR_MAX, true, true);
        slider((std::string(t("ui.freecam_table.saturation")) + "##freecam_sat").c_str(), options.freecamSat, SETTINGS_FREECAM_SATURATION_MIN,
               SETTINGS_FREECAM_SATURATION_MAX);
        ImGui::PopItemWidth();
        ImGui::SeparatorText(t("ui.freecam_table.hide"));
        toggle((std::string(t("ui.freecam_table.player")) + "##freecam_hideplayer").c_str(), options.isFreecamHideSelfEnabled);
        toggle((std::string(t("ui.freecam_table.party")) + "##freecam_hideparty").c_str(), options.isFreecamHidePartyEnabled);
        toggle((std::string(t("ui.freecam_table.other_pawns")) + "##freecam_hideothers").c_str(), options.isFreecamHideOthersEnabled);
        toggle((std::string(t("ui.freecam_table.vehicles")) + "##freecam_hidevehicle").c_str(), options.isFreecamHideVehicleEnabled);
        ImGui::Unindent();
    } else {
        ImGui::PopItemWidth();
    }
    if (ImGui::Button((std::string(ICON_FA_LOCATION_ARROW " ") + t("ui.freecam_table.reset_to_player") + "##freecam_reset").c_str())) {
        app.engine().postGameThreadTask([]() {
            Application::instance().freecam().resetFreecamToPlayer();
        });
    }
    const std::string state = isFreecamActive() ? (freecamWanted().load() ? Translation::instance().translate("ui.freecam_table.status_state.free")
                                                                          : Translation::instance().translate("ui.freecam_table.status_state.disabling"))
                                                : (freecamWanted().load() ? Translation::instance().translate("ui.freecam_table.status_state.enabling")
                                                                          : Translation::instance().translate("ui.freecam_table.status_state.attached"));
    ImGui::TextDisabled(t("ui.freecam_table.status"), state.c_str(), dragModeSuffix());
    ImGui::TextDisabled(t("ui.freecam_table.instructions"));
    Application::instance().photoOverlay().renderUi();
    ImGui::Unindent();
}