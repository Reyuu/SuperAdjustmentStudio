#include "snapshots.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "json.hpp"
#include "snapshot_json.h"
#include "application.h"

#include "animation.h"
#include "bones.h"
#include "freecam.h"
#include "lights.h"
#include "lut_effect.h"
#include "particles.h"
#include "photo_overlay.h"
#include "prefabs.h"
#include "ui.h"
#include "ui/helpers/raii_guards.h"
#include "vfx.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>

using json = nlohmann::json;

static const char* currentGameId() {
#if defined(SDK_TARGET_LE1)
    return "LE1";
#elif defined(SDK_TARGET_LE3)
    return "LE3";
#else
    return "LE2";
#endif
}

// default to LE2 -> mostly historic
static std::string snapshotGame(const json& snapshot) {
    if (snapshot.is_object()) {
        auto it = snapshot.find("game");
        if (it != snapshot.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }
    return "LE2";
}

static bool snapshotGameMatches(const json& snapshot) {
    if (!snapshot.is_object()) {
        return true;
    }
    return snapshotGame(snapshot) == currentGameId();
}

static void notifyWrongGame(const std::string& filename, const json& snapshot) {
    char buf[512];
    snprintf(buf, sizeof(buf), t("ui.snapshots.wrong_game"), filename.c_str(), snapshotGame(snapshot).c_str(), currentGameId());
    Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeWarning, 4.0);
}

#pragma region // Getters
json SnapshotsManager::getCameraData() const {
    json cameraData;
    Freecam& freecam = Application::instance().freecam();
    CameraOffsets* cameraOffsets = &freecam.getOffsets();
    cameraData["position"] = json::array({cameraOffsets->position.X, cameraOffsets->position.Y, cameraOffsets->position.Z});
    cameraData["rotation"] = json::array({cameraOffsets->rotation.Roll, cameraOffsets->rotation.Pitch, cameraOffsets->rotation.Yaw});
    cameraData["freecamEnabled"] = freecam.isFreecamActive();
    return cameraData;
}

json SnapshotsManager::getFovRollData() const {
    json fovRollData;
    const SettingsOptions& o = Application::instance().settings().options;
    fovRollData["fov"] = o.freecamFOV;
    fovRollData["roll"] = o.freecamRoll;
    return fovRollData;
}

json SnapshotsManager::getCameraPostProcessData() const {
    json ppData;
    const SettingsOptions& o = Application::instance().settings().options;
    ppData["adjustEnabled"] = o.isFreecamAdjustEnabled;
    ppData["dofEnabled"] = o.isFreecamDofEnabled;
    ppData["bloomEnabled"] = o.isFreecamBloomEnabled;
    ppData["colorEnabled"] = o.isFreecamColorEnabled;
    ppData["bloomThreshold"] = o.freecamBloomThreshold;
    ppData["bloomScale"] = o.freecamBloomScale;
    ppData["dofDistance"] = o.freecamDofDistance;
    ppData["dofInnerRadius"] = o.freecamDofInnerRadius;
    ppData["dofFstop"] = o.freecamDofFStop;
    ppData["dofIntensity"] = o.freecamDofIntensity;
    ppData["saturation"] = o.freecamSaturation;
    ppData["contrast"] = o.freecamContrast;
    ppData["bright"] = o.freecamBright;
    ppData["sat"] = o.freecamSat;
    return ppData;
}

json SnapshotsManager::getFilterData() const {
    json filterData;
    PhotoOverlay& overlay = Application::instance().photoOverlay();
    filterData["enabled"] = overlay.filterState;
    filterData["tint"] = json::array({overlay.tintColor[0], overlay.tintColor[1], overlay.tintColor[2]});
    filterData["tintStrength"] = overlay.tintStrength;
    filterData["grainIntensity"] = overlay.grainIntensity;
    filterData["grainOpacity"] = overlay.grainOpacity;
    filterData["noFog"] = overlay.noFogState;
    filterData["noLensFlare"] = overlay.noLensFlareState;
    return filterData;
}

json SnapshotsManager::getLutData() const {
    json lutData;
    const SettingsOptions& o = Application::instance().settings().options;
    json layers = json::array();
    for (const auto& layer : o.lutLayers) {
        json out;
        out["ref"] = layer.ref;
        out["enabled"] = layer.enabled;
        out["blend"] = layer.blend;
        out["chroma"] = layer.chroma;
        out["luma"] = layer.luma;
        out["useDepth"] = layer.useDepth;
        out["depthFocus"] = layer.depthFocus;
        out["depthRange"] = layer.depthRange;
        out["blendMin"] = layer.blendMin;
        out["blendMax"] = layer.blendMax;
        out["previewDepth"] = layer.previewDepth;
        out["heatPreview"] = layer.heatPreview;
        layers.push_back(out);
    }
    lutData["layers"] = layers;
    lutData["depthCapture"] = o.lutDepthCapture;
    lutData["depthShowTexture"] = o.lutDepthShowTexture;
    lutData["depthSource"] = o.lutDepthSource;
    lutData["depthLinearize"] = o.lutDepthLinearize;
    lutData["depthInvert"] = o.lutDepthInvert;
    lutData["depthNear"] = o.lutDepthNear;
    lutData["depthFar"] = o.lutDepthFar;
    lutData["depthEveryN"] = o.lutDepthEveryN;
    return lutData;
}

json SnapshotsManager::getAnimationData() const {
    json animData;
    Animation& anim = Application::instance().animation();
    if (anim.playedAnimName.empty()) {
        return animData;
    }
    animData["pawn"] = anim.playedPawnName;
    animData["anim"] = anim.playedAnimName;
    animData["loop"] = anim.playedLoop;
    return animData;
}

json SnapshotsManager::getVfxData() const {
    json vfxData = json::array();
    VFXManager& vfx = Application::instance().vfx();
    std::lock_guard<std::mutex> lock(vfx.vfxMtx);
    for (const auto& entry : vfx.vfxEntries) {
        json out;
        out["template"] = entry.name;
        out["path"] = entry.path;
        out["pawn"] = entry.pawnName;
        out["bone"] = entry.boneName;
        out["lifeTime"] = entry.lifeTime;
        out["loop"] = entry.loop;
        out["loopDelay"] = entry.loopDelay;
        vfxData.push_back(out);
    }
    return vfxData;
}

json SnapshotsManager::getBonesData() const {
    json bonesData = json::array();
    Bones& bones = Application::instance().bones();
    std::lock_guard<std::mutex> lock(bones.bonePose.mtx);
    if (bones.bonePose.posed.empty()) {
        return bonesData;
    }
    json entry;
    entry["pawn"] = bones.bonePose.pawn;
    entry["target"] = (int)bones.bonePose.target;
    json posed = json::array();
    for (const auto& bone : bones.bonePose.posed) {
        json out;
        out["index"] = bone.index;
        out["name"] = bone.boneName;
        out["position"] = json::array({bone.pos[0], bone.pos[1], bone.pos[2]});
        out["rotation"] = json::array({bone.rot[0], bone.rot[1], bone.rot[2]});
        out["scale"] = json::array({bone.scale[0], bone.scale[1], bone.scale[2]});
        posed.push_back(out);
    }
    entry["bones"] = posed;
    bonesData.push_back(entry);
    return bonesData;
}

json SnapshotsManager::getSelectionData() const {
    json selData;
    UI& ui = Application::instance().ui();
    selData["name"] = ui.getSelectedPawnName();
    const Transform& t = ui.getSelectedTransform();
    json transform;
    transform["position"] = json::array({t.pos[0], t.pos[1], t.pos[2]});
    transform["rotation"] = json::array({t.rot[0], t.rot[1], t.rot[2]});
    transform["scale"] = json::array({t.scale[0], t.scale[1], t.scale[2]});
    selData["transform"] = transform;
    return selData;
}

json SnapshotsManager::getLightingData() const {
    json lightsData = json::array();
    LightManager& lights = Application::instance().lights();
    Engine& engine = Application::instance().engine();
    std::lock_guard<std::mutex> lock(lights.lightsMtx);
    for (const auto& entry : lights.lightEntries) {
        if (!isLiveObject(entry.actor)) {
            continue;
        }
        LightSettings s;
        if (!LightManager::readLightSettings(entry.actor, s)) {
            continue;
        }
        Transform t;
        engine.loadTransformFromActor(entry.actor, t);
        json out;
        out["name"] = entry.name;
        out["type"] = entry.type;
        out["class"] = "Engine.SpotLightMovable";
        json transform;
        transform["position"] = json::array({t.pos[0], t.pos[1], t.pos[2]});
        transform["rotation"] = json::array({t.rot[0], t.rot[1], t.rot[2]});
        transform["scale"] = json::array({t.scale[0], t.scale[1], t.scale[2]});
        out["transform"] = transform;
        json settings;
        settings["enabled"] = s.enabled;
        settings["color"] = json::array({s.color[0], s.color[1], s.color[2]});
        settings["brightness"] = s.brightness;
        settings["radius"] = s.radius;
        settings["falloff"] = s.falloffExponent;
        settings["innerCone"] = s.innerConeAngle;
        settings["outerCone"] = s.outerConeAngle;
        settings["shaftCone"] = s.lightShaftConeAngle;
        settings["bloomScale"] = s.bloomScale;
        settings["bloomThreshold"] = s.bloomThreshold;
        settings["bloomBlend"] = s.bloomScreenBlendThreshold;
        settings["bloomTint"] = json::array({s.bloomTint[0], s.bloomTint[1], s.bloomTint[2]});
        settings["shadowRadiusMult"] = s.shadowRadiusMultiplier;
        settings["shadowProj"] = s.shadowProjectionTechnique;
        settings["shadowFilter"] = s.shadowFilterQuality;
        settings["shadowMode"] = s.lightShadowMode;
        settings["castShadows"] = s.castShadows;
        settings["castDynamicShadows"] = s.castDynamicShadows;
        settings["renderShafts"] = s.renderLightShafts;
        settings["isPoint"] = s.isPoint;
        settings["isSpot"] = s.isSpot;
        out["settings"] = settings;
        lightsData.push_back(out);
    }
    return lightsData;
}

json SnapshotsManager::getPrefabsData() const {
    json prefabsData = json::array();
    PrefabManager& prefabs = Application::instance().prefabs();
    Engine& engine = Application::instance().engine();
    std::lock_guard<std::recursive_mutex> lock(prefabs.mutex);
    for (const auto& entry : prefabs.prefabEntries) {
        if (entry.actors.empty() || !isLiveObject(entry.actors[0])) {
            continue;
        }
        std::string package;
        for (const auto& t : prefabs.available) {
            if (t.fullName == entry.prefabName) {
                package = t.package;
                break;
            }
        }
        Transform t;
        engine.loadTransformFromActor(entry.actors[0], t);
        json out;
        out["name"] = entry.name;
        out["prefab"] = entry.prefabName;
        out["package"] = package;
        json transform;
        transform["position"] = json::array({t.pos[0], t.pos[1], t.pos[2]});
        transform["rotation"] = json::array({t.rot[0], t.rot[1], t.rot[2]});
        transform["scale"] = json::array({t.scale[0], t.scale[1], t.scale[2]});
        out["transform"] = transform;
        prefabsData.push_back(out);
    }
    return prefabsData;
}

json SnapshotsManager::getParticlesData() const {
    json particlesData = json::array();
    ParticleManager& particles = Application::instance().particles();
    Engine& engine = Application::instance().engine();
    std::lock_guard<std::mutex> lock(particles.particleMtx);
    for (const auto& entry : particles.particleEntries) {
        if (!isLiveObject(entry.emitterActor)) {
            continue;
        }
        Transform t;
        engine.loadTransformFromActor(entry.emitterActor, t);
        json out;
        out["template"] = entry.name;
        out["path"] = entry.path;
        out["pawn"] = entry.pawnName;
        json transform;
        transform["position"] = json::array({t.pos[0], t.pos[1], t.pos[2]});
        transform["rotation"] = json::array({t.rot[0], t.rot[1], t.rot[2]});
        transform["scale"] = json::array({t.scale[0], t.scale[1], t.scale[2]});
        out["transform"] = transform;
        out["lifeTime"] = entry.lifeTime;
        out["loop"] = entry.loop;
        out["loopDelay"] = entry.loopDelay;
        particlesData.push_back(out);
    }
    return particlesData;
}
#pragma endregion

#pragma region // Setters
void SnapshotsManager::applyCameraData(const json& cameraData) {
    if (!cameraData.is_object()) {
        return;
    }
    bool hasPos = SAS_JSON_HAS_ARR3(cameraData, "position");
    bool hasRot = SAS_JSON_HAS_ARR3(cameraData, "rotation");
    if (!hasPos && !hasRot) {
        return;
    }
    float posVals[3] = {0.0f, 0.0f, 0.0f};
    int rotVals[3] = {0, 0, 0};
    SAS_JSON_READ_ARR3(cameraData, "position", posVals);
    SAS_JSON_READ_ARR3(cameraData, "rotation", rotVals);
    FVector pos{posVals[0], posVals[1], posVals[2]};
    FRotator rot{0, 0, 0};
    rot.Roll = rotVals[0];
    rot.Pitch = rotVals[1];
    rot.Yaw = rotVals[2];
    bool enable = false;
    SAS_JSON_READ_BOOL(cameraData, "freecamEnabled", enable);
    Application::instance().engine().postGameThreadTask([pos, rot, hasPos, hasRot, enable]() {
        Freecam& freecam = Application::instance().freecam();
        if (enable) {
            freecam.freecamWanted().store(true);
            if (!freecam.isFreecamActive()) {
                freecam.applyFreecamEnabled(true);
            }
        }
        if (hasPos) {
            freecam.offsets.position = pos;
        }
        if (hasRot) {
            freecam.offsets.rotation = rot;
        }
        freecam.applyFreecamLook(false);
    });
}

void SnapshotsManager::applyFovRollData(const json& fovRollData) {
    if (!fovRollData.is_object()) {
        return;
    }
    SettingsOptions& o = Application::instance().settings().options;
    SAS_JSON_READ_NUMBER(fovRollData, "fov", o.freecamFOV);
    SAS_JSON_READ_NUMBER(fovRollData, "roll", o.freecamRoll);
    Application::instance().settings().markChanged();
    Application::instance().engine().postGameThreadTask([]() {
        Application::instance().freecam().applyFreecamLook(false);
    });
}

void SnapshotsManager::applyCameraPostProcessData(const json& cameraPostProcessData) {
    if (!cameraPostProcessData.is_object()) {
        return;
    }
    SettingsOptions& o = Application::instance().settings().options;
    SAS_JSON_READ_BOOL(cameraPostProcessData, "adjustEnabled", o.isFreecamAdjustEnabled);
    SAS_JSON_READ_BOOL(cameraPostProcessData, "dofEnabled", o.isFreecamDofEnabled);
    SAS_JSON_READ_BOOL(cameraPostProcessData, "bloomEnabled", o.isFreecamBloomEnabled);
    SAS_JSON_READ_BOOL(cameraPostProcessData, "colorEnabled", o.isFreecamColorEnabled);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "bloomThreshold", o.freecamBloomThreshold);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "bloomScale", o.freecamBloomScale);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "dofDistance", o.freecamDofDistance);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "dofInnerRadius", o.freecamDofInnerRadius);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "dofFstop", o.freecamDofFStop);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "dofIntensity", o.freecamDofIntensity);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "saturation", o.freecamSaturation);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "contrast", o.freecamContrast);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "bright", o.freecamBright);
    SAS_JSON_READ_NUMBER(cameraPostProcessData, "sat", o.freecamSat);
    Application::instance().settings().markChanged();
    Application::instance().engine().postGameThreadTask([]() {
        Application::instance().freecam().applyFreecamLook(false);
    });
}

void SnapshotsManager::applyFilterData(const json& filterData) {
    if (!filterData.is_object()) {
        return;
    }
    PhotoOverlay& overlay = Application::instance().photoOverlay();
    SAS_JSON_READ_BOOL(filterData, "enabled", overlay.filterState);
    SAS_JSON_READ_ARR3(filterData, "tint", overlay.tintColor);
    SAS_JSON_READ_NUMBER(filterData, "tintStrength", overlay.tintStrength);
    SAS_JSON_READ_NUMBER(filterData, "grainIntensity", overlay.grainIntensity);
    SAS_JSON_READ_NUMBER(filterData, "grainOpacity", overlay.grainOpacity);
    bool noFog = overlay.noFogState;
    SAS_JSON_READ_BOOL(filterData, "noFog", noFog);
    if (noFog != overlay.noFogState) {
        overlay.noFogState = noFog;
        Application::instance().engine().consoleCommand("show fog");
    }
    bool noLensFlare = overlay.noLensFlareState;
    SAS_JSON_READ_BOOL(filterData, "noLensFlare", noLensFlare);
    if (noLensFlare != overlay.noLensFlareState) {
        overlay.noLensFlareState = noLensFlare;
        Application::instance().engine().consoleCommand("show lensflares");
    }
}

void SnapshotsManager::applyLutData(const json& lutData) {
    if (!lutData.is_object()) {
        return;
    }
    SettingsOptions& o = Application::instance().settings().options;
    if (lutData.contains("layers") && lutData["layers"].is_array()) {
        std::vector<LutLayerOptions> layers;
        for (const auto& layer : lutData["layers"]) {
            if (!layer.is_object()) {
                continue;
            }
            if ((int)layers.size() >= LUT_MAX_LAYERS) {
                break;
            }
            LutLayerOptions out;
            SAS_JSON_READ_STRING(layer, "ref", out.ref);
            if (out.ref.empty()) {
                continue;
            }
            SAS_JSON_READ_BOOL(layer, "enabled", out.enabled);
            SAS_JSON_READ_NUMBER(layer, "blend", out.blend);
            SAS_JSON_READ_NUMBER(layer, "chroma", out.chroma);
            SAS_JSON_READ_NUMBER(layer, "luma", out.luma);
            SAS_JSON_READ_BOOL(layer, "useDepth", out.useDepth);
            SAS_JSON_READ_NUMBER(layer, "depthFocus", out.depthFocus);
            SAS_JSON_READ_NUMBER(layer, "depthRange", out.depthRange);
            SAS_JSON_READ_NUMBER(layer, "blendMin", out.blendMin);
            SAS_JSON_READ_NUMBER(layer, "blendMax", out.blendMax);
            SAS_JSON_READ_BOOL(layer, "previewDepth", out.previewDepth);
            SAS_JSON_READ_BOOL(layer, "heatPreview", out.heatPreview);
            layers.push_back(out);
        }
        o.lutLayers = layers;
    }
    SAS_JSON_READ_BOOL(lutData, "depthCapture", o.lutDepthCapture);
    SAS_JSON_READ_BOOL(lutData, "depthShowTexture", o.lutDepthShowTexture);
    SAS_JSON_READ_STRING(lutData, "depthSource", o.lutDepthSource);
    SAS_JSON_READ_BOOL(lutData, "depthLinearize", o.lutDepthLinearize);
    SAS_JSON_READ_BOOL(lutData, "depthInvert", o.lutDepthInvert);
    SAS_JSON_READ_NUMBER(lutData, "depthNear", o.lutDepthNear);
    SAS_JSON_READ_NUMBER(lutData, "depthFar", o.lutDepthFar);
    SAS_JSON_READ_NUMBER(lutData, "depthEveryN", o.lutDepthEveryN);
    Application::instance().settings().markChanged();
}

void SnapshotsManager::applyAnimationData(const json& animationData) {
    if (!animationData.is_object()) {
        return;
    }
    std::string pawn;
    std::string anim;
    bool loop = true;
    SAS_JSON_READ_STRING(animationData, "pawn", pawn);
    SAS_JSON_READ_STRING(animationData, "anim", anim);
    SAS_JSON_READ_BOOL(animationData, "loop", loop);
    if (anim.empty()) {
        if (!pawn.empty()) {
            Application::instance().animation().resetAnimation(pawn);
        }
        return;
    }
    Application::instance().animation().playAnimation(pawn, anim, loop);
}

void SnapshotsManager::applyVfxData(const json& vfxData) {
    if (!vfxData.is_array()) {
        return;
    }
    struct VfxSpawnItem {
            std::string templateName;
            std::string pawn;
            std::string bone;
            float lifeTime = 9999.0f;
            bool loop = false;
            double loopDelay = 0.0;
    };
    std::vector<VfxSpawnItem> items;
    for (const auto& item : vfxData) {
        if (!item.is_object()) {
            continue;
        }
        VfxSpawnItem out;
        SAS_JSON_READ_STRING(item, "template", out.templateName);
        SAS_JSON_READ_STRING(item, "pawn", out.pawn);
        SAS_JSON_READ_STRING(item, "bone", out.bone);
        SAS_JSON_READ_NUMBER(item, "lifeTime", out.lifeTime);
        SAS_JSON_READ_BOOL(item, "loop", out.loop);
        SAS_JSON_READ_NUMBER(item, "loopDelay", out.loopDelay);
        if (out.templateName.empty() || out.pawn.empty() || out.bone.empty()) {
            continue;
        }
        items.push_back(out);
    }
    double spawnTime = ImGui::GetTime();
    Logger->debug("snapshots: applying " + std::to_string(items.size()) + " vfx entries");
    Application::instance().engine().postGameThreadTask([items = std::move(items), spawnTime]() {
        VFXManager& vfx = Application::instance().vfx();
        Engine& engine = Application::instance().engine();
        for (const auto& item : items) {
            auto* vfxTemplate = VFXManager::findTemplateByName(item.templateName);
            AActor* actor = engine.findActorByName(item.pawn);
            if (!vfxTemplate || !actor) {
                continue;
            }
            size_t before = vfx.vfxEntries.size();
            vfx.addVFX(vfxTemplate, actor, item.bone, item.lifeTime, spawnTime);
            if (vfx.vfxEntries.size() > before) {
                std::lock_guard<std::mutex> lock(vfx.vfxMtx);
                vfx.vfxEntries.back().loop = item.loop;
                vfx.vfxEntries.back().loopDelay = item.loopDelay;
            }
        }
    });
}

void SnapshotsManager::applyBonesData(const json& bonesData) {
    if (!bonesData.is_array()) {
        return;
    }
    Bones& bones = Application::instance().bones();
    if (bonesData.empty()) {
        return;
    }
    for (const auto& entry : bonesData) {
        if (!entry.is_object()) {
            continue;
        }
        std::string pawn;
        SAS_JSON_READ_STRING(entry, "pawn", pawn);
        int targetInt = MESH_BODY;
        SAS_JSON_READ_NUMBER(entry, "target", targetInt);
        if (pawn.empty() || (targetInt != MESH_BODY && targetInt != MESH_HEAD)) {
            continue;
        }
        if (!entry.contains("bones") || !entry["bones"].is_array()) {
            continue;
        }
        for (const auto& item : entry["bones"]) {
            if (!item.is_object()) {
                continue;
            }
            BonePoseInfo pose;
            SAS_JSON_READ_NUMBER(item, "index", pose.index);
            SAS_JSON_READ_STRING(item, "name", pose.boneName);
            SAS_JSON_READ_ARR3(item, "position", pose.pos);
            SAS_JSON_READ_ARR3(item, "rotation", pose.rot);
            SAS_JSON_READ_ARR3(item, "scale", pose.scale);
            if (pose.index < 0) {
                continue;
            }
            bones.setBonePose(pawn, (MeshTarget)targetInt, pose);
        }
    }
}

void SnapshotsManager::applySelectionData(const json& selectionData) {
    if (!selectionData.is_object()) {
        return;
    }
    std::string name;
    SAS_JSON_READ_STRING(selectionData, "name", name);
    if (name.empty()) {
        return;
    }
    AActor* actor = Application::instance().engine().findActorByName(name);
    if (!actor) {
        return;
    }
    Application::instance().ui().selectActor(actor);
}

void SnapshotsManager::applyLightingData(const json& lightingData) {
    if (!lightingData.is_array()) {
        return;
    }
    struct LightSpawnItem {
            std::string cls = "Engine.SpotLightMovable";
            std::string type = "Spot light";
            Transform t;
            LightSettings s;
    };
    std::vector<LightSpawnItem> items;
    for (const auto& item : lightingData) {
        if (!item.is_object()) {
            continue;
        }
        LightSpawnItem out;
        SAS_JSON_READ_STRING(item, "class", out.cls);
        SAS_JSON_READ_STRING(item, "type", out.type);
        if (item.contains("transform") && item["transform"].is_object()) {
            const json& transform = item["transform"];
            SAS_JSON_READ_ARR3(transform, "position", out.t.pos);
            SAS_JSON_READ_ARR3(transform, "rotation", out.t.rot);
            SAS_JSON_READ_ARR3(transform, "scale", out.t.scale);
        }
        if (item.contains("settings") && item["settings"].is_object()) {
            const json& settings = item["settings"];
            SAS_JSON_READ_BOOL(settings, "enabled", out.s.enabled);
            SAS_JSON_READ_ARR3(settings, "color", out.s.color);
            SAS_JSON_READ_NUMBER(settings, "brightness", out.s.brightness);
            SAS_JSON_READ_NUMBER(settings, "radius", out.s.radius);
            SAS_JSON_READ_NUMBER(settings, "falloff", out.s.falloffExponent);
            SAS_JSON_READ_NUMBER(settings, "innerCone", out.s.innerConeAngle);
            SAS_JSON_READ_NUMBER(settings, "outerCone", out.s.outerConeAngle);
            SAS_JSON_READ_NUMBER(settings, "shaftCone", out.s.lightShaftConeAngle);
            SAS_JSON_READ_NUMBER(settings, "bloomScale", out.s.bloomScale);
            SAS_JSON_READ_NUMBER(settings, "bloomThreshold", out.s.bloomThreshold);
            SAS_JSON_READ_NUMBER(settings, "bloomBlend", out.s.bloomScreenBlendThreshold);
            SAS_JSON_READ_ARR3(settings, "bloomTint", out.s.bloomTint);
            SAS_JSON_READ_NUMBER(settings, "shadowRadiusMult", out.s.shadowRadiusMultiplier);
            SAS_JSON_READ_NUMBER(settings, "shadowProj", out.s.shadowProjectionTechnique);
            SAS_JSON_READ_NUMBER(settings, "shadowFilter", out.s.shadowFilterQuality);
            SAS_JSON_READ_NUMBER(settings, "shadowMode", out.s.lightShadowMode);
            SAS_JSON_READ_BOOL(settings, "castShadows", out.s.castShadows);
            SAS_JSON_READ_BOOL(settings, "castDynamicShadows", out.s.castDynamicShadows);
            SAS_JSON_READ_BOOL(settings, "renderShafts", out.s.renderLightShafts);
            SAS_JSON_READ_BOOL(settings, "isPoint", out.s.isPoint);
            SAS_JSON_READ_BOOL(settings, "isSpot", out.s.isSpot);
        }
        items.push_back(out);
    }
    Logger->debug("snapshots: applying " + std::to_string(items.size()) + " light entries");
    Application::instance().engine().postGameThreadTask([items = std::move(items)]() {
        LightManager& lights = Application::instance().lights();
        Engine& engine = Application::instance().engine();
        for (const auto& item : items) {
            AActor* actor = engine.spawnClass(item.cls, item.t);
            if (!actor) {
                continue;
            }
            lights.addLight(actor, item.type);
            lights.applyLightProperties(actor, item.s);
        }
    });
}

void SnapshotsManager::applyPrefabsData(const json& prefabsData) {
    if (!prefabsData.is_array()) {
        return;
    }
    struct PrefabSpawnItem {
            std::string prefab;
            Transform t;
    };
    std::vector<PrefabSpawnItem> items;
    for (const auto& item : prefabsData) {
        if (!item.is_object()) {
            continue;
        }
        PrefabSpawnItem out;
        SAS_JSON_READ_STRING(item, "prefab", out.prefab);
        if (out.prefab.empty()) {
            continue;
        }
        if (item.contains("transform") && item["transform"].is_object()) {
            const json& transform = item["transform"];
            SAS_JSON_READ_ARR3(transform, "position", out.t.pos);
            SAS_JSON_READ_ARR3(transform, "rotation", out.t.rot);
            SAS_JSON_READ_ARR3(transform, "scale", out.t.scale);
        }
        items.push_back(out);
    }
    Logger->debug("snapshots: applying " + std::to_string(items.size()) + " prefab entries");
    PrefabManager& prefabs = Application::instance().prefabs();
    Engine& engine = Application::instance().engine();
    for (const auto& item : items) {
        size_t before = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(prefabs.mutex);
            before = prefabs.prefabEntries.size();
        }
        prefabs.spawnPrefab(item.prefab);
        std::lock_guard<std::recursive_mutex> lock(prefabs.mutex);
        if (prefabs.prefabEntries.size() <= before) {
            continue;
        }
        PrefabEntry& spawned = prefabs.prefabEntries.back();
        if (spawned.prefabName != item.prefab || spawned.actors.empty() || !isLiveObject(spawned.actors[0])) {
            continue;
        }
        Transform cur;
        engine.loadTransformFromActor(spawned.actors[0], cur);
        float dx = item.t.pos[0] - cur.pos[0];
        float dy = item.t.pos[1] - cur.pos[1];
        float dz = item.t.pos[2] - cur.pos[2];
        engine.setTransform(spawned.actors[0], item.t);
        for (size_t i = 1; i < spawned.actors.size(); ++i) {
            if (!isLiveObject(spawned.actors[i])) {
                continue;
            }
            Transform at;
            engine.loadTransformFromActor(spawned.actors[i], at);
            at.pos[0] += dx;
            at.pos[1] += dy;
            at.pos[2] += dz;
            engine.setTransform(spawned.actors[i], at);
        }
    }
}

void SnapshotsManager::applyParticlesData(const json& particlesData) {
    if (!particlesData.is_array()) {
        return;
    }
    struct ParticleSpawnItem {
            std::string templateName;
            std::string pawn;
            Transform t;
            float lifeTime = 9999.0f;
            bool loop = false;
            double loopDelay = 0.0;
    };
    std::vector<ParticleSpawnItem> items;
    for (const auto& item : particlesData) {
        if (!item.is_object()) {
            continue;
        }
        ParticleSpawnItem out;
        SAS_JSON_READ_STRING(item, "template", out.templateName);
        SAS_JSON_READ_STRING(item, "pawn", out.pawn);
        SAS_JSON_READ_NUMBER(item, "lifeTime", out.lifeTime);
        SAS_JSON_READ_BOOL(item, "loop", out.loop);
        SAS_JSON_READ_NUMBER(item, "loopDelay", out.loopDelay);
        if (out.templateName.empty() || out.pawn.empty()) {
            continue;
        }
        if (item.contains("transform") && item["transform"].is_object()) {
            const json& transform = item["transform"];
            SAS_JSON_READ_ARR3(transform, "position", out.t.pos);
            SAS_JSON_READ_ARR3(transform, "rotation", out.t.rot);
            SAS_JSON_READ_ARR3(transform, "scale", out.t.scale);
        }
        items.push_back(out);
    }
    double spawnTime = ImGui::GetTime();
    Logger->debug("snapshots: applying " + std::to_string(items.size()) + " particle entries");
    Application::instance().engine().postGameThreadTask([items = std::move(items), spawnTime]() {
        ParticleManager& particles = Application::instance().particles();
        Engine& engine = Application::instance().engine();
        for (const auto& item : items) {
            UParticleSystem* particleTemplate = ParticleManager::findTemplateByName(item.templateName);
            AActor* owner = engine.findActorByName(item.pawn);
            if (!particleTemplate || !owner) {
                continue;
            }
            size_t before = particles.particleEntries.size();
            particles.addParticle(particleTemplate, owner, item.t, item.lifeTime, spawnTime);
            if (particles.particleEntries.size() > before) {
                std::lock_guard<std::mutex> lock(particles.particleMtx);
                particles.particleEntries.back().loop = item.loop;
                particles.particleEntries.back().loopDelay = item.loopDelay;
            }
        }
    });
}
#pragma endregion

#pragma region // Save / Load / UI

static const SnapshotSectionInfo* findSectionByKey(const std::string& key) {
    for (const auto& info : SNAPSHOT_SECTION_INFOS) {
        if (key == info.key) {
            return &info;
        }
    }
    return nullptr;
}

static const char* SNAPSHOT_SAVE_POPUP_ID = "SASSnapshotSave";
static const char* SNAPSHOT_LOAD_POPUP_ID = "SASSnapshotLoad";

std::filesystem::path SnapshotsManager::snapshotsDir() {
    return Application::instance().settings().configDir() / "snapshots";
}

void SnapshotsManager::migrateLegacyPresetsDir() {
    std::error_code ec;
    std::filesystem::path legacy = Application::instance().settings().configDir() / "presets";
    std::filesystem::path dir = snapshotsDir();
    if (!std::filesystem::exists(legacy, ec) || ec) {
        return;
    }
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::rename(legacy, dir, ec);
        ec.clear();
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(legacy, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::filesystem::path target = dir / entry.path().filename();
        if (!std::filesystem::exists(target, ec)) {
            std::filesystem::rename(entry.path(), target, ec);
            ec.clear();
        }
    }
}

std::string SnapshotsManager::sanitizeSnapshotName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += c;
        } else if (c == ' ') {
            out += '_';
        }
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    if (out.empty()) {
        out = "snapshot";
    }
    return out;
}

bool SnapshotsManager::hasSectionData(SnapshotSection section) {
    switch (section) {
        case SnapshotCamera:
        case SnapshotFovRoll:
        case SnapshotFilter:
        case SnapshotCameraPP: {
            return true;
        }
        case SnapshotLut: {
            return !Application::instance().settings().options.lutLayers.empty();
        }
        case SnapshotAnimation: {
            return !Application::instance().animation().playedAnimName.empty();
        }
        case SnapshotVfx: {
            VFXManager& vfx = Application::instance().vfx();
            std::lock_guard<std::mutex> lock(vfx.vfxMtx);
            return !vfx.vfxEntries.empty();
        }
        case SnapshotBones: {
            Bones& bones = Application::instance().bones();
            std::lock_guard<std::mutex> lock(bones.bonePose.mtx);
            return !bones.bonePose.posed.empty();
        }
        case SnapshotSelection: {
            return !Application::instance().ui().getSelectedPawnName().empty();
        }
        case SnapshotLighting: {
            LightManager& lights = Application::instance().lights();
            std::lock_guard<std::mutex> lock(lights.lightsMtx);
            return !lights.lightEntries.empty();
        }
        case SnapshotPrefabs: {
            PrefabManager& prefabs = Application::instance().prefabs();
            std::lock_guard<std::recursive_mutex> lock(prefabs.mutex);
            return !prefabs.prefabEntries.empty();
        }
        case SnapshotParticles: {
            ParticleManager& particles = Application::instance().particles();
            std::lock_guard<std::mutex> lock(particles.particleMtx);
            return !particles.particleEntries.empty();
        }
        default: {
            return false;
        }
    }
}

void SnapshotsManager::saveSnapshot() {
    std::string name = saveName;
    std::string filename;
    bool merging = false;
    if (name.empty()) {
        if (lastLoadedFile.empty()) {
            Application::instance().ui().toastManager.addToastNotification(t("ui.snapshots.name_empty"), ToastTypeWarning, 3.0);
            return;
        }
        filename = lastLoadedFile;
        name = lastLoadedName;
        if (name.empty()) {
            name = std::filesystem::path(filename).stem().string();
        }
        merging = true;
    } else {
        filename = sanitizeSnapshotName(name) + ".json";
        std::filesystem::path existingPath = snapshotsDir() / filename;
        if (std::filesystem::exists(existingPath) && !saveConfirmOverwrite) {
            saveConfirmOverwrite = true;
            return;
        }
    }
    saveConfirmOverwrite = false;
    json snapshot;
    if (merging) {
        std::ifstream in(snapshotsDir() / filename);
        if (in.is_open()) {
            try {
                in >> snapshot;
            } catch (const std::exception& e) {
                Logger->warn("snapshots: failed to parse '{}': {}", filename, e.what());
            }
        }
        if (!snapshot.is_object()) {
            snapshot = json::object();
        }
    }
    snapshot["name"] = name;
    snapshot["game"] = currentGameId();
    json sections = json::array();
    for (const auto& info : SNAPSHOT_SECTION_INFOS) {
        if (!saveSections[info.section]) {
            continue;
        }
        switch (info.section) {
            case SnapshotCamera: {
                snapshot[info.key] = getCameraData();
                break;
            }
            case SnapshotFovRoll: {
                snapshot[info.key] = getFovRollData();
                break;
            }
            case SnapshotFilter: {
                snapshot[info.key] = getFilterData();
                break;
            }
            case SnapshotLut: {
                snapshot[info.key] = getLutData();
                break;
            }
            case SnapshotAnimation: {
                snapshot[info.key] = getAnimationData();
                break;
            }
            case SnapshotVfx: {
                snapshot[info.key] = getVfxData();
                break;
            }
            case SnapshotBones: {
                snapshot[info.key] = getBonesData();
                break;
            }
            case SnapshotSelection: {
                snapshot[info.key] = getSelectionData();
                break;
            }
            case SnapshotLighting: {
                snapshot[info.key] = getLightingData();
                break;
            }
            case SnapshotPrefabs: {
                snapshot[info.key] = getPrefabsData();
                break;
            }
            case SnapshotParticles: {
                snapshot[info.key] = getParticlesData();
                break;
            }
            case SnapshotCameraPP: {
                snapshot[info.key] = getCameraPostProcessData();
                break;
            }
            default:
                continue;
        }
        sections.push_back(info.key);
    }
    if (sections.empty()) {
        Application::instance().ui().toastManager.addToastNotification(t("ui.snapshots.nothing_to_save"), ToastTypeWarning, 3.0);
        return;
    }
    if (merging && snapshot.contains("sections") && snapshot["sections"].is_array()) {
        json merged = json::array();
        for (const auto& s : snapshot["sections"]) {
            if (s.is_string()) {
                merged.push_back(s.get<std::string>());
            }
        }
        for (const auto& saved : sections) {
            bool known = false;
            for (const auto& m : merged) {
                if (m.get<std::string>() == saved.get<std::string>()) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                merged.push_back(saved.get<std::string>());
            }
        }
        sections = merged;
    }
    snapshot["sections"] = sections;

    std::filesystem::path path = snapshotsDir() / filename;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.save_failed"), name.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        return;
    }
    std::ofstream out(path);
    if (!out.is_open()) {
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.save_failed"), name.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        return;
    }
    out << snapshot.dump(4);
    out.flush();
    if (!out) {
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.save_failed"), name.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        return;
    }
    char buf[512];
    snprintf(buf, sizeof(buf), t("ui.snapshots.saved_ok"), name.c_str());
    Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeSuccess, 3.0);
    lastLoadedFile = filename;
    lastLoadedName = name;
    showSaveWizard = false;
    refreshSnapshotFiles();
}

void SnapshotsManager::applyLoadedSnapshot(const json& snapshot) {
    if (!snapshot.is_object()) {
        return;
    }
    for (const auto& info : SNAPSHOT_SECTION_INFOS) {
        if (!loadSections[info.section]) {
            continue;
        }
        auto it = snapshot.find(info.key);
        if (it == snapshot.end()) {
            continue;
        }
        Logger->debug("snapshots: applying section '" + std::string(info.key) + "'");
        switch (info.section) {
            case SnapshotCamera: {
                applyCameraData(*it);
                break;
            }
            case SnapshotFovRoll: {
                applyFovRollData(*it);
                break;
            }
            case SnapshotFilter: {
                applyFilterData(*it);
                break;
            }
            case SnapshotLut: {
                applyLutData(*it);
                break;
            }
            case SnapshotAnimation: {
                applyAnimationData(*it);
                break;
            }
            case SnapshotVfx: {
                applyVfxData(*it);
                break;
            }
            case SnapshotBones: {
                applyBonesData(*it);
                break;
            }
            case SnapshotSelection: {
                applySelectionData(*it);
                break;
            }
            case SnapshotLighting: {
                applyLightingData(*it);
                break;
            }
            case SnapshotPrefabs: {
                applyPrefabsData(*it);
                break;
            }
            case SnapshotParticles: {
                applyParticlesData(*it);
                break;
            }
            case SnapshotCameraPP: {
                applyCameraPostProcessData(*it);
                break;
            }
            default: {
                break;
            }
        }
    }
}

void SnapshotsManager::loadSnapshot(std::string filename) {
    std::filesystem::path path = snapshotsDir() / filename;
    std::ifstream in(path);
    if (!in.is_open()) {
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.load_failed"), filename.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        return;
    }
    try {
        json snapshot;
        in >> snapshot;
        if (!snapshotGameMatches(snapshot)) {
            notifyWrongGame(filename, snapshot);
            return;
        }
        for (int i = 0; i < SnapshotSectionCount; ++i) {
            loadSections[i] = true;
        }
        applyLoadedSnapshot(snapshot);
        std::string name = filename;
        if (snapshot.contains("name") && snapshot["name"].is_string()) {
            name = snapshot["name"].get<std::string>();
        }
        lastLoadedFile = filename;
        lastLoadedName = name;
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.loaded_ok"), name.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeSuccess, 3.0);
    } catch (const std::exception& e) {
        Logger->warn("snapshots: failed to parse '{}': {}", path.string(), e.what());
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.load_failed"), filename.c_str());
        Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
    }
}

void SnapshotsManager::openSaveWizard() {
    Logger->debug("snapshots: opening save wizard");
    saveModalLogged = false;
    saveName[0] = '\0';
    saveConfirmOverwrite = false;
    for (int i = 0; i < SnapshotSectionCount; ++i) {
        saveSections[i] = hasSectionData((SnapshotSection)i);
    }
    showSaveWizard = true;
    savePopupOpened = false;
}

void SnapshotsManager::openLoadWizard() {
    Logger->debug("snapshots: opening load wizard");
    loadModalLogged = false;
    refreshSnapshotFiles();
    for (int i = 0; i < SnapshotSectionCount; ++i) {
        loadSections[i] = true;
    }
    showLoadWizard = true;
    loadPopupOpened = false;
}

void SnapshotsManager::refreshSnapshotFiles() {
    migrateLegacyPresetsDir();
    std::string selected;
    if (loadIndex >= 0 && loadIndex < (int)loadFiles.size()) {
        selected = loadFiles[loadIndex].filename;
    }
    loadFiles.clear();
    loadIndex = -1;
    std::error_code ec;
    std::filesystem::create_directories(snapshotsDir(), ec);
    for (const auto& entry : std::filesystem::directory_iterator(snapshotsDir(), ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".json") {
            continue;
        }
        SnapshotFileEntry file;
        file.filename = entry.path().filename().string();
        file.name = entry.path().stem().string();
        std::ifstream in(entry.path());
        if (in.is_open()) {
            try {
                json snapshot;
                in >> snapshot;
                if (!snapshotGameMatches(snapshot)) {
                    continue;
                }
                if (snapshot.contains("name") && snapshot["name"].is_string()) {
                    file.name = snapshot["name"].get<std::string>();
                }
                if (snapshot.contains("sections") && snapshot["sections"].is_array()) {
                    for (const auto& s : snapshot["sections"]) {
                        if (s.is_string()) {
                            file.sections.push_back(s.get<std::string>());
                        }
                    }
                } else {
                    for (const auto& info : SNAPSHOT_SECTION_INFOS) {
                        if (snapshot.contains(info.key)) {
                            file.sections.push_back(info.key);
                        }
                    }
                }
            } catch (const std::exception& e) {
                Logger->warn("snapshots: failed to parse '{}': {}", entry.path().string(), e.what());
            }
        }
        for (const auto& key : file.sections) {
            const SnapshotSectionInfo* info = findSectionByKey(key);
            if (info) {
                file.icons += info->icon;
                file.icons += " ";
            }
        }
        loadFiles.push_back(file);
    }
    std::sort(loadFiles.begin(), loadFiles.end(), [](const SnapshotFileEntry& a, const SnapshotFileEntry& b) {
        return a.filename < b.filename;
    });
    for (int i = 0; i < (int)loadFiles.size(); ++i) {
        if (loadFiles[i].filename == selected) {
            loadIndex = i;
            break;
        }
    }
    if (loadIndex < 0 && !loadFiles.empty()) {
        loadIndex = 0;
    }
}

void SnapshotsManager::renderUi() {
    if (ImGui::BeginMenu((std::string(ICON_FA_BOOKMARK " ") + t("ui.snapshots.menu")).c_str())) {
        if (ImGui::MenuItem((std::string(ICON_FA_FLOPPY_DISK " ") + t("ui.snapshots.save")).c_str())) {
            openSaveWizard();
        }
        if (ImGui::MenuItem((std::string(ICON_FA_FOLDER_OPEN " ") + t("ui.snapshots.load")).c_str())) {
            openLoadWizard();
        }
        ImGui::EndMenu();
    }
}

void SnapshotsManager::renderWizards() {
    renderSaveWizard();
    renderLoadWizard();
}

void SnapshotsManager::renderSaveWizard() {
    if (!showSaveWizard) {
        return;
    }
    if (!savePopupOpened) {
        ImGui::OpenPopup(SNAPSHOT_SAVE_POPUP_ID);
        savePopupOpened = true;
    }
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    std::string saveTitle = (std::string(ICON_FA_FLOPPY_DISK " ") + t("ui.snapshots.save_title")) + "###" + SNAPSHOT_SAVE_POPUP_ID;
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.3f));
    if (!ImGui::BeginPopupModal(saveTitle.c_str(), &showSaveWizard, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!saveModalLogged) {
            Logger->debug("snapshots: save modal NOT opening");
            saveModalLogged = true;
        }
        ImGui::PopStyleColor();
        return;
    }
    if (!saveModalLogged) {
        Logger->debug("snapshots: save modal visible");
        saveModalLogged = true;
    }
    ImGui::InputTextWithHint("##sas_snapshot_name", t("ui.snapshots.name_hint"), saveName, sizeof(saveName));
    if (saveName[0] == '\0' && !lastLoadedFile.empty()) {
        char buf[512];
        snprintf(buf, sizeof(buf), t("ui.snapshots.overwrite_last"), lastLoadedFile.c_str());
        ImGui::TextDisabled("%s", buf);
    }
    ImGui::Separator();
    for (const auto& info : SNAPSHOT_SECTION_INFOS) {
        bool checked = saveSections[info.section];
        ImGui::BeginDisabled(!hasSectionData(info.section));
        if (ImGui::Checkbox((std::string(info.icon) + " " + t(info.labelKey)).c_str(), &checked)) {
            saveSections[info.section] = checked;
        }
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    if (saveConfirmOverwrite) {
        ImGui::TextWrapped("%s", t("ui.snapshots.overwrite_confirm"));
    }
    if (ImGui::Button((std::string(ICON_FA_FLOPPY_DISK " ") + t("ui.snapshots.save_button")).c_str())) {
        saveSnapshot();
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_XMARK " ") + t("ui.snapshots.cancel_button")).c_str())) {
        showSaveWizard = false;
    }
    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

void SnapshotsManager::renderLoadWizard() {
    if (!showLoadWizard) {
        return;
    }
    if (!loadPopupOpened) {
        ImGui::OpenPopup(SNAPSHOT_LOAD_POPUP_ID);
        loadPopupOpened = true;
    }
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_FirstUseEver);
    std::string loadTitle = (std::string(ICON_FA_FOLDER_OPEN " ") + t("ui.snapshots.load_title")) + "###" + SNAPSHOT_LOAD_POPUP_ID;
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.3f));
    if (!ImGui::BeginPopupModal(loadTitle.c_str(), &showLoadWizard)) {
        if (!loadModalLogged) {
            Logger->debug("snapshots: load modal NOT opening");
            loadModalLogged = true;
        }
        ImGui::PopStyleColor();
        return;
    }
    if (!loadModalLogged) {
        Logger->debug("snapshots: load modal visible");
        loadModalLogged = true;
    }
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##sas_snapshot_refresh")) {
        refreshSnapshotFiles();
    }
    if (loadFiles.empty()) {
        ImGui::TextDisabled("%s", t("ui.snapshots.no_snapshots"));
    } else {
        {
            ChildScope child("##sas_snapshot_list", ImVec2(270, 300), true);
            if (child.open) {
                for (int i = 0; i < (int)loadFiles.size(); ++i) {
                    std::string loadLabel = loadFiles[i].name + "##" + loadFiles[i].filename;
                    if (ImGui::Selectable(loadLabel.c_str(), i == loadIndex)) {
                        loadIndex = i;
                        for (int s = 0; s < SnapshotSectionCount; ++s) {
                            loadSections[s] = true;
                        }
                    }
                    if (!loadFiles[i].icons.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", loadFiles[i].icons.c_str());
                    }
                }
            }
        }
        ImGui::SameLine();
        {
            ChildScope child("##sas_snapshot_detail", ImVec2(0, 300), true);
            if (child.open && loadIndex >= 0 && loadIndex < (int)loadFiles.size()) {
                const SnapshotFileEntry& file = loadFiles[loadIndex];
                for (const auto& key : file.sections) {
                    const SnapshotSectionInfo* info = findSectionByKey(key);
                    if (!info) {
                        continue;
                    }
                    bool checked = loadSections[info->section];
                    if (ImGui::Checkbox((std::string(info->icon) + " " + t(info->labelKey)).c_str(), &checked)) {
                        loadSections[info->section] = checked;
                    }
                }
            }
        }
    }
    ImGui::BeginDisabled(loadFiles.empty() || loadIndex < 0 || loadIndex >= (int)loadFiles.size());
    if (ImGui::Button((std::string(ICON_FA_FOLDER_OPEN " ") + t("ui.snapshots.load_button")).c_str())) {
        std::string filename = loadFiles[loadIndex].filename;
        std::filesystem::path path = snapshotsDir() / filename;
        std::ifstream in(path);
        if (!in.is_open()) {
            char buf[512];
            snprintf(buf, sizeof(buf), t("ui.snapshots.load_failed"), filename.c_str());
            Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        } else {
            try {
                json snapshot;
                in >> snapshot;
                if (!snapshotGameMatches(snapshot)) {
                    notifyWrongGame(filename, snapshot);
                } else {
                    applyLoadedSnapshot(snapshot);
                    std::string name = filename;
                    if (snapshot.contains("name") && snapshot["name"].is_string()) {
                        name = snapshot["name"].get<std::string>();
                    }
                    lastLoadedFile = filename;
                    lastLoadedName = name;
                    char buf[512];
                    snprintf(buf, sizeof(buf), t("ui.snapshots.loaded_ok"), name.c_str());
                    Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeSuccess, 3.0);
                    showLoadWizard = false;
                }
            } catch (const std::exception& e) {
                Logger->warn("snapshots: failed to parse '{}': {}", path.string(), e.what());
                char buf[512];
                snprintf(buf, sizeof(buf), t("ui.snapshots.load_failed"), filename.c_str());
                Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(loadFiles.empty() || loadIndex < 0 || loadIndex >= (int)loadFiles.size());
    if (ImGui::Button((std::string(ICON_FA_TRASH " ") + t("ui.snapshots.delete_button")).c_str())) {
        std::string filename = loadFiles[loadIndex].filename;
        std::error_code ec;
        std::filesystem::remove(snapshotsDir() / filename, ec);
        if (ec) {
            char buf[512];
            snprintf(buf, sizeof(buf), t("ui.snapshots.delete_failed"), filename.c_str());
            Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeError, 4.0);
        } else {
            char buf[512];
            snprintf(buf, sizeof(buf), t("ui.snapshots.deleted_ok"), filename.c_str());
            Application::instance().ui().toastManager.addToastNotification(buf, ToastTypeSuccess, 3.0);
            refreshSnapshotFiles();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_XMARK " ") + t("ui.snapshots.cancel_button")).c_str())) {
        showLoadWizard = false;
    }
    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
#pragma endregion
