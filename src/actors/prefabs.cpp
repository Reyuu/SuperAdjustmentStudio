#include "prefabs.h"
#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "IconsFontAwesome6.h"
#include "application.h"
#include "engine.h"
#include "imgui.h"
#include "logger.h"
#include "ui/helpers/raii_guards.h"
#include "ui/helpers/toast_notifications.h"
#include "util.h"
#include <algorithm>
#include <numbers>
#include <sstream>
#include <unordered_map>

#include "tracy.h"

static UStaticMeshComponent* findStaticMeshComp(AActor* a) {
    if (!a) {
        return nullptr;
    }

    for (int i = 0; i < (int)a->Components.Count(); ++i) {
        UObject* c = a->Components(i);
        if (c && c->IsA(UStaticMeshComponent::StaticClass())) {
            return static_cast<UStaticMeshComponent*>(c);
        }
    }
    return nullptr;
}

static AActor* getClassDefaultActor(UClass* cls) {
    if (!UObject::GObjObjects) {
        return nullptr;
    }

    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (o && o->Class == cls && o->IsA(AActor::StaticClass())) {
            std::string n = FStringToUtf8(o->GetName());
            if (n.rfind("Default__", 0) == 0 || n.rfind("default__", 0) == 0) {
                return static_cast<AActor*>(o);
            }
        }
    }
    return nullptr;
}

static UPrefab* findPrefabByName(const std::string& name) {
    UPrefab* found = nullptr;
    std::string needle = toLowerStr(name);
    forEachOf<UPrefab>([&](UPrefab* p) {
        if (!p) {
            return;
        }

        std::string n = toLowerStr(FStringToUtf8(p->GetName()));
        if (needle.empty() || n == needle || n.find(needle) != std::string::npos) {
            found = p;
        }
    });
    return found;
}

void PrefabManager::collectPrefabs() {
    ZoneScopedN("Prefabs::collectPrefabs");
    std::lock_guard<std::recursive_mutex> lock(mutex);
    available.clear();
    if (!UObject::GObjObjects) {
        return;
    }

    forEachOf<UPrefab>([&](UPrefab* prefab) {
        if (!prefab) {
            return;
        }
        std::string name = FStringToUtf8(prefab->GetName());
        if (name.rfind("Default__", 0) == 0) {
            return;
        }
        UObject* outer = prefab->Outer;
        while (outer && outer->Outer) {
            outer = outer->Outer;
        }
        std::string package = outer ? FStringToUtf8(outer->GetName()) : "(unknown)";
        PrefabTemplate t;
        t.fullName = FStringToUtf8(prefab->GetFullName());
        t.name = name;
        t.package = package;
        t.nameLower = toLowerStr(name);
        t.archetypeCount = (int)prefab->PrefabArchetypes.Count();
        available.push_back(t);
    });

    std::sort(available.begin(), available.end(), [](const PrefabTemplate& a, const PrefabTemplate& b) {
        if (a.package != b.package) {
            return a.package < b.package;
        }
        return a.name < b.name;
    });
}

// yaw/pitch (degrees) for a body at `from` to face `to`
static void yawPitchTowards(const FVector& from, const FVector& to, float& outPitchDeg, float& outYawDeg) {
    FVector dir{to.X - from.X, to.Y - from.Y, to.Z - from.Z};
    outYawDeg = atan2f(dir.Y, dir.X) * (180.0f / std::numbers::pi_v<float>);
    outPitchDeg = atan2f(dir.Z, sqrtf(dir.X * dir.X + dir.Y * dir.Y)) * (180.0f / std::numbers::pi_v<float>);
}

// aims a light at lookAt by setting its yaw/pitch rotation (roll left at 0)
static void orientLightTowards(AActor* light, const FVector& lightPos, const FVector& lookAt) {
    float pitchDeg, yawDeg;
    yawPitchTowards(lightPos, lookAt, pitchDeg, yawDeg);
    light->SetRotation(FRotator{DegreesToUnrealRotationUnits(pitchDeg), DegreesToUnrealRotationUnits(yawDeg), 0});
}

// world-space origin/extent of target's mesh bounds, expanded by DrawScale3D (min 80 per axis)
static bool computeActorWorldBounds(AActor* target, FVector& outOrigin, FVector& outExtent) {
    UStaticMeshComponent* comp = findStaticMeshComp(target);
    if (!comp) {
        return false;
    }

    FBoxSphereBounds bounds;
    comp->GetUnscaledBounds(&bounds);
    FVector extent = bounds.BoxExtent;
    extent.X *= target->DrawScale3D.X;
    extent.Y *= target->DrawScale3D.Y;
    extent.Z *= target->DrawScale3D.Z;
    FVector localOrigin = bounds.Origin;
    localOrigin.X *= target->DrawScale3D.X;
    localOrigin.Y *= target->DrawScale3D.Y;
    localOrigin.Z *= target->DrawScale3D.Z;
    FVector worldOriginOffset = RotateVector(localOrigin, target->Rotation);
    FVector origin = target->Location;
    origin.X += worldOriginOffset.X;
    origin.Y += worldOriginOffset.Y;
    origin.Z += worldOriginOffset.Z;

    // ensure minimum extent per axis
    if (extent.X < 5) {
        extent.X = 80;
    }
    if (extent.Y < 5) {
        extent.Y = 80;
    }
    if (extent.Z < 5) {
        extent.Z = 80;
    }

    outOrigin = origin;
    outExtent = extent;
    return true;
}

// world position just outside corner `lightIndex` (bit pattern selects +/- per axis)
static FVector cornerLightPosition(size_t lightIndex, const FVector& origin, const FVector& extent, const FRotator& targetRot, float expansion) {
    int sx = (lightIndex & 1) ? 1 : -1;
    int sy = (lightIndex & 2) ? 1 : -1;
    int sz = (lightIndex & 4) ? 1 : -1;

    FVector localCorner{sx * extent.X, sy * extent.Y, sz * extent.Z};
    FVector worldCornerOffset = RotateVector(localCorner, targetRot);
    float len = std::numbers::sqrt3_v<float>;
    FVector localDiag{sx / len * expansion, sy / len * expansion, sz / len * expansion};
    FVector worldDiag = RotateVector(localDiag, targetRot);
    return {origin.X + worldCornerOffset.X + worldDiag.X, origin.Y + worldCornerOffset.Y + worldDiag.Y, origin.Z + worldCornerOffset.Z + worldDiag.Z};
}

// world position just outside face `faceIdx` (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z)
static FVector faceLightPosition(size_t faceIdx, const FVector& origin, const FVector& extent, const FRotator& targetRot, float expansion) {
    static const FVector unitCenters[6] = {
        {1,  0,  0 },
        {-1, 0,  0 },
        {0,  1,  0 },
        {0,  -1, 0 },
        {0,  0,  1 },
        {0,  0,  -1}
    };
    if (faceIdx >= 6) {
        return origin;
    }

    const FVector& u = unitCenters[faceIdx];
    FVector local{u.X * extent.X, u.Y * extent.Y, u.Z * extent.Z};
    FVector worldOff = RotateVector(local, targetRot);
    FVector localOut{u.X * expansion, u.Y * expansion, u.Z * expansion};
    FVector worldOut = RotateVector(localOut, targetRot);
    return {origin.X + worldOff.X + worldOut.X, origin.Y + worldOff.Y + worldOut.Y, origin.Z + worldOff.Z + worldOut.Z};
}

// moves a corner light (index lightIndex, corner selected by its bit pattern) to sit just outside that corner
static void repositionCornerLight(AActor* light, size_t lightIndex, const FVector& origin, const FVector& extent, const FRotator& targetRot, float expansion) {
    FVector pos = cornerLightPosition(lightIndex, origin, extent, targetRot, expansion);
    light->SetLocation(pos);
    orientLightTowards(light, pos, origin);
}

// moves a face light (index lightIndex, face centers[faceIdx]) to sit just outside that face
static void repositionFaceLight(AActor* light, size_t faceIdx, const FVector& origin, const FVector& extent, const FRotator& targetRot, float expansion) {
    FVector pos = faceLightPosition(faceIdx, origin, extent, targetRot, expansion);
    light->SetLocation(pos);
    orientLightTowards(light, pos, origin);
}

// repositions entry's lights around target using whichever layout matches the light count
static void repositionPrefabLights(PrefabEntry& entry, const FRotator& targetRot, const FVector& origin, const FVector& extent, float expansion) {
    size_t n = entry.lights.size();
    auto placeCorner = [&](size_t lightIndex) {
        if (lightIndex < n && isLiveObject(entry.lights[lightIndex])) {
            repositionCornerLight(entry.lights[lightIndex], lightIndex, origin, extent, targetRot, expansion);
        }
    };
    auto placeFace = [&](size_t lightIndex, size_t faceIdx) {
        if (lightIndex < n && isLiveObject(entry.lights[lightIndex])) {
            repositionFaceLight(entry.lights[lightIndex], faceIdx, origin, extent, targetRot, expansion);
        }
    };

    // choose layout based on light count
    if (n == 6) {
        for (size_t i = 0; i < 6; ++i) {
            placeFace(i, i);
        }
    } else if (n == 8) {
        for (size_t i = 0; i < 8; ++i) {
            placeCorner(i);
        }
    } else if (n >= 14) {
        for (size_t i = 0; i < 8; ++i) {
            placeCorner(i);
        }
        for (size_t i = 0; i < 6; ++i) {
            placeFace(8 + i, i);
        }
    } else {
        for (size_t i = 0; i < n && i < 8; ++i) {
            placeCorner(i);
        }
    }
}

void PrefabManager::onActorMoved(AActor* actor) {
    if (!isLiveObject(actor)) {
        return;
    }

    if (ImGui::GetTime() - lastMaterialUpdate < 0.25f) {
        return;
    }
    lastMaterialUpdate = ImGui::GetTime();

    // lock the prefab manager while updating actor positions
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try {
        for (auto& entry : prefabEntries) {
            bool ownsActor = std::any_of(entry.actors.begin(), entry.actors.end(), [&](AActor* a) {
                return isLiveObject(a) && a == actor;
            });
            if (!ownsActor) {
                continue;
            }

            // material fix removed to avoid stall/black; only reposition the lights
            FVector origin, extent;
            if (computeActorWorldBounds(actor, origin, extent)) {
                repositionPrefabLights(entry, actor->Rotation, origin, extent, lightExpansion);
            }
            return;
        }
    } catch (const std::exception& e) {
        Logger->debug(std::string("onActorMoved exception: ") + e.what());
    } catch (...) {
        Logger->debug("onActorMoved unknown exception");
    }
}

// nudge lights of the selected prefab entry by index by 1uu
void PrefabManager::nudgeLights(int idx) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (idx < 0 || idx >= (int)prefabEntries.size()) {
        return;
    }

    PrefabEntry& entry = prefabEntries[idx];
    for (AActor* light : entry.lights) {
        if (!isLiveObject(light)) {
            continue;
        }

        FVector loc = light->Location;
        FVector nudge{loc.X + 1.0f, loc.Y, loc.Z};
        light->SetLocation(nudge);
        light->SetLocation(loc);
    }
}

// force update the material of the selected prefab entry
void PrefabManager::forceUpdateMaterial() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (selectedActive < 0 || selectedActive >= (int)prefabEntries.size()) {
        Logger->debug("forceUpdateMaterial: no selection");
        return;
    }

    PrefabEntry& entry = prefabEntries[selectedActive];
    int updated = 0;
    for (AActor* actor : entry.actors) {
        if (!isLiveObject(actor)) {
            continue;
        }

        for (int ci = 0; ci < (int)actor->Components.Count(); ++ci) {
            UObject* co = actor->Components(ci);
            if (!co || !co->IsA(UMeshComponent::StaticClass())) {
                continue;
            }

            UMeshComponent* comp = static_cast<UMeshComponent*>(co);
            int n = comp->GetNumElements();
            for (int i = 0; i < n; ++i) {
                UMaterialInterface* cur = comp->GetMaterial(i);
                if (!cur) {
                    continue;
                }

                // force reassign same material to trigger update
                comp->SetMaterial(i, cur);
                // also force MIC update if it's a MIC
                if (cur->IsA(UMaterialInstanceConstant::StaticClass())) {
                    UMaterialInstanceConstant* mic = static_cast<UMaterialInstanceConstant*>(cur);
                    // touch a param to dirty
                    mic->ClearParameterValues();
                    // reassign
                    comp->SetMaterial(i, mic);
                }
            }

            comp->PrestreamTextures(5.0f, 1, 0);
            if (comp->IsA(UStaticMeshComponent::StaticClass())) {
                UStaticMeshComponent* smc = static_cast<UStaticMeshComponent*>(comp);
                if (smc->StaticMesh) {
                    UStaticMesh* m = smc->StaticMesh;
                    smc->StaticMesh = nullptr;
                    smc->SetStaticMesh(m, 1);
                }
            }

            // also handle m_aEffectsMaterialMICs
            for (int ei = 0; ei < (int)comp->m_aEffectsMaterialMICs.Count(); ++ei) {
                UMaterialInstanceConstant* emic = comp->m_aEffectsMaterialMICs(ei);
                if (!emic) {
                    continue;
                }

                // force update via clear and re-set (no-op but dirties)
                emic->ClearParameterValues();
            }
            ++updated;
        }
    }

    Logger->debug("forceUpdateMaterial: updated " + std::to_string(updated) + " comps for '" + entry.name + "'");
    Application::instance().ui().toastManager.addToastNotification("Material update forced", ToastTypeSuccess, 2.0);
}

// update the light expansion of the selected prefab entry
void PrefabManager::updateLightExpansion(float offset) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    lightExpansion = offset;
    if (selectedActive < 0 || selectedActive >= (int)prefabEntries.size()) {
        return;
    }

    PrefabEntry& entry = prefabEntries[selectedActive];
    if (entry.actors.empty() || !isLiveObject(entry.actors[0])) {
        return;
    }

    AActor* target = entry.actors[0];
    FVector origin, extent;
    if (computeActorWorldBounds(target, origin, extent)) {
        repositionPrefabLights(entry, target->Rotation, origin, extent, offset);
    }
}

// spawns a SpotLightMovable at t, attaches it to entryIdx's target actor, and applies current light settings
void PrefabManager::spawnPrefabLight(const Transform& t, int entryIdx) {
    Application::instance().engine().postGameThreadTask([this, t, entryIdx]() {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        std::wstring wName = L"Class Engine.SpotLightMovable";
        UClass* cls = UObject::FindClass(wName.c_str());
        DWORD orig = 0;
        if (cls) {
            orig = cls->ClassFlags;
            cls->ClassFlags |= CLASS_Placeable;
            cls->ClassFlags &= ~CLASS_NotPlaceable;
        }

        AActor* cdo = cls ? getClassDefaultActor(cls) : nullptr;
        bool cs = cdo ? cdo->bStatic : false, cn = cdo ? cdo->bNoDelete : false;
        if (cdo) {
            cdo->bStatic = 0;
            cdo->bNoDelete = 0;
        }

        AActor* light = Application::instance().engine().spawnClass("Engine.SpotLightMovable", t);
        if (cdo) {
            cdo->bStatic = cs;
            cdo->bNoDelete = cn;
        }
        if (cls) {
            cls->ClassFlags = orig;
        }
        if (!light || !isLiveObject(light)) {
            return;
        }

        if (entryIdx >= 0 && entryIdx < (int)prefabEntries.size() && !prefabEntries[entryIdx].actors.empty() &&
            isLiveObject(prefabEntries[entryIdx].actors[0])) {
            AActor* target = prefabEntries[entryIdx].actors[0];
            light->bHardAttach = 1;
            light->SetBase(target, FVector{0, 0, 0}, nullptr, SFXName());
            prefabEntries[entryIdx].lights.push_back(light);
        }

        ALight* al = static_cast<ALight*>(light);
        if (!isLiveObject(al) || !isLiveObject(al->LightComponent)) {
            return;
        }

        FColor col{(unsigned char)(lightColor[2] * 255), (unsigned char)(lightColor[1] * 255), (unsigned char)(lightColor[0] * 255), 255}; // BGR
        al->LightComponent->Brightness = lightBrightness;
        al->LightComponent->SetLightProperties(lightBrightness, col, al->LightComponent->Function);
        al->LightComponent->bEnabled = 1;
        if (al->LightComponent->IsA(USpotLightComponent::StaticClass())) {
            auto* slc = static_cast<USpotLightComponent*>(al->LightComponent);
            slc->Radius = lightRadius;
            slc->InnerConeAngle = 30.0f;
            slc->OuterConeAngle = 60.0f;
        } else if (al->LightComponent->IsA(UPointLightComponent::StaticClass())) {
            auto* plc = static_cast<UPointLightComponent*>(al->LightComponent);
            plc->Radius = lightRadius;
        }
        al->LightComponent->UpdateLightShaftParameters();
    });
}

void PrefabManager::spawnCornerLights() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (selectedActive < 0 || selectedActive >= (int)prefabEntries.size()) {
        Application::instance().ui().toastManager.addToastNotification("Select another object to be able to spawn the prefab!", ToastTypeError, 2.0);
        return;
    }

    PrefabEntry& entry = prefabEntries[selectedActive];
    if (entry.actors.empty() || !isLiveObject(entry.actors[0])) {
        return;
    }

    AActor* target = entry.actors[0];
    UStaticMeshComponent* comp = findStaticMeshComp(target);
    if (!comp || !comp->StaticMesh) {
        return;
    }

    FVector origin, extent;
    if (!computeActorWorldBounds(target, origin, extent)) {
        return;
    }

    int idx = selectedActive;
    for (size_t i = 0; i < 8; ++i) {
        FVector pos = cornerLightPosition(i, origin, extent, target->Rotation, lightExpansion);
        float pitchDeg, yawDeg;
        yawPitchTowards(pos, origin, pitchDeg, yawDeg);
        Transform t;
        t.pos[0] = pos.X;
        t.pos[1] = pos.Y;
        t.pos[2] = pos.Z;
        t.rot[0] = pitchDeg;
        t.rot[1] = yawDeg;
        t.rot[2] = 0;
        t.scale[0] = t.scale[1] = t.scale[2] = 1;
        spawnPrefabLight(t, idx);
    }
    Application::instance().ui().toastManager.addToastNotification("Spawned 8 corner lights for the selected object", ToastTypeSuccess, 2.0);
}

void PrefabManager::spawnFaceLights() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (selectedActive < 0 || selectedActive >= (int)prefabEntries.size()) {
        Application::instance().ui().toastManager.addToastNotification("Select a spawned prefab first!", ToastTypeError, 2.0);
        return;
    }
    PrefabEntry& entry = prefabEntries[selectedActive];
    if (entry.actors.empty() || !isLiveObject(entry.actors[0])) {
        return;
    }
    AActor* target = entry.actors[0];
    UStaticMeshComponent* comp = findStaticMeshComp(target);
    if (!comp || !comp->StaticMesh) {
        return;
    }

    FVector origin, extent;
    if (!computeActorWorldBounds(target, origin, extent)) {
        return;
    }

    int idx = selectedActive;
    for (size_t i = 0; i < 6; ++i) {
        FVector pos = faceLightPosition(i, origin, extent, target->Rotation, lightExpansion);
        float pitchDeg, yawDeg;
        yawPitchTowards(pos, origin, pitchDeg, yawDeg);
        Transform t;
        t.pos[0] = pos.X;
        t.pos[1] = pos.Y;
        t.pos[2] = pos.Z;
        t.rot[0] = pitchDeg;
        t.rot[1] = yawDeg;
        t.rot[2] = 0;
        t.scale[0] = t.scale[1] = t.scale[2] = 1;
        spawnPrefabLight(t, idx);
    }
    Application::instance().ui().toastManager.addToastNotification("Spawned 6 face lights for the selected object", ToastTypeSuccess, 2.0);
}

void PrefabManager::spawnLightsByConfig(LightConfig cfg) {
    // lock not needed here; callees lock
    switch (cfg) {
        case LightConfig::Corner: {
            spawnCornerLights();
            break;
        }
        case LightConfig::Face: {
            spawnFaceLights();
            break;
        }
        case LightConfig::CornerFace: {
            spawnCornerLights();
            spawnFaceLights();
            break;
        }
    }
}

void PrefabManager::spawnPrefab(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    UPrefab* prefab = findPrefabByName(name);
    if (!prefab) {
        Logger->debug("spawnPrefab: prefab no longer exists: " + name);
        return;
    }

    AActor* caller = Application::instance().engine().findActorByName("");
    if (!caller) {
        Logger->debug("spawnPrefab: no actor available to spawn from");
        return;
    }

    Transform t = Application::instance().ui().getSpawnTransform();
    FVector baseLoc{t.pos[0], t.pos[1], t.pos[2]};
    FRotator baseRot{DegreesToUnrealRotationUnits(t.rot[0]), DegreesToUnrealRotationUnits(t.rot[1]), DegreesToUnrealRotationUnits(t.rot[2])};
    std::vector<AActor*> spawnedActors;
    const int archTotal = (int)prefab->PrefabArchetypes.Count();
    const int archCap = (archTotal > 256) ? 256 : archTotal;
    if (archCap == 0) {
        Logger->debug("spawnPrefab: '" + name + "' has no actor archetypes");
        return;
    }

    Logger->debug("spawnPrefab: spawning '" + name + "' archetypes=" + std::to_string(archCap) + " (total reported " + std::to_string(archTotal) + ")");
    for (int i = 0; i < archCap; ++i) {
        UObject* arch = prefab->PrefabArchetypes(i);
        if (!arch || !arch->IsA(AActor::StaticClass())) {
            continue;
        }

        AActor* arcActor = static_cast<AActor*>(arch);
        UClass* cls = arcActor->Class;
        FVector loc = baseLoc;
        FRotator rot = baseRot;
        loc.X += arcActor->Location.X;
        loc.Y += arcActor->Location.Y;
        loc.Z += arcActor->Location.Z;
        rot.Pitch += arcActor->Rotation.Pitch;
        rot.Yaw += arcActor->Rotation.Yaw;
        rot.Roll += arcActor->Rotation.Roll;

        const DWORD originalFlags = cls->ClassFlags;
        cls->ClassFlags |= CLASS_Placeable;
        cls->ClassFlags &= ~CLASS_NotPlaceable;
        AActor* cdo = getClassDefaultActor(cls);
        bool cdoStatic = cdo ? cdo->bStatic : false, cdoNoDelete = cdo ? cdo->bNoDelete : false;
        if (cdo) {
            cdo->bStatic = 0;
            cdo->bNoDelete = 0;
        }

        AActor* spawned = caller->Spawn(cls, NULL, SFXName(), loc, rot, arcActor, NULL, 1, 0);
        if (cdo) {
            cdo->bStatic = cdoStatic;
            cdo->bNoDelete = cdoNoDelete;
        }

        cls->ClassFlags = originalFlags;
        if (!spawned) {
            Logger->debug("spawnPrefab:  Spawn returned NULL for '" + FStringToUtf8(arcActor->GetName()) + "'");
            continue;
        }

        spawned->bStatic = 0;
        spawned->bMovable = 1;
        spawned->bNoDelete = 0;
        // via naNuke: accept dynamic light instead of baked
        for (int ci = 0; ci < (int)spawned->Components.Count(); ++ci) {
            UObject* co = spawned->Components(ci);
            if (!co || !co->IsA(UPrimitiveComponent::StaticClass())) {
                continue;
            }

            UPrimitiveComponent* prim = static_cast<UPrimitiveComponent*>(co);
            prim->bAcceptsLights = 1;
            prim->bAcceptsDynamicLights = 1;
            prim->bUsePrecomputedShadows = 0;
            prim->bForceDirectLightMap = 1;
            prim->CastShadow = 1;
            prim->bCastDynamicShadow = 1;
            if (co->IsA(UStaticMeshComponent::StaticClass())) {
                UStaticMeshComponent* smc = static_cast<UStaticMeshComponent*>(co);
                smc->LightmassSettings.bUseTwoSidedLighting = 0;
                smc->bUsePrecomputedShadows = 0;
            }
        }
        spawnedActors.push_back(spawned);
    }

    if (spawnedActors.empty()) {
        Logger->debug("spawnPrefab: prefab produced no actors: " + name);
        Application::instance().ui().toastManager.addToastNotification("Spawn produced no actors (see log)! " + name, ToastTypeError, 4.0);
        return;
    }

    PrefabEntry entry;
    entry.name = FStringToUtf8(spawnedActors[0]->GetName());
    entry.prefabName = FStringToUtf8(prefab->GetFullName());
    entry.actors = spawnedActors;
    prefabEntries.push_back(entry);
    selectedActive = (int)prefabEntries.size() - 1;

    Application::instance().ui().selectActor(spawnedActors[0]);
    std::ostringstream ss;
    ss << "Spawned prefab " << FStringToUtf8(prefab->GetName()) << " (" << spawnedActors.size() << " actor" << (spawnedActors.size() == 1 ? "" : "s") << ")";
    Application::instance().ui().toastManager.addToastNotification(ss.str(), ToastTypeSuccess, 2.0);
    Logger->debug("spawnPrefab: '" + name + "' spawned " + std::to_string(spawnedActors.size()) + " actors");

    // auto-spawn 8 corner lights
    spawnCornerLights();
    forceUpdateMaterial();
}

void PrefabManager::removePrefab(const PrefabEntry& entry) {
    removePrefabByName(entry.name, entry.prefabName);
}

void PrefabManager::removePrefabByName(const std::string& name, const std::string& prefabName) {
    PrefabEntry entry;
    int idx = -1;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        idx = findEntryIndexByName(name, prefabName);
        if (idx < 0) {
            return;
        }

        entry = prefabEntries[idx];
        prefabEntries.erase(prefabEntries.begin() + idx);
        fixupSelectedActive();
    }

    // use same mechanism as Target menu (Engine::removeActor)
    for (AActor* actor : entry.actors) {
        if (!actor) {
            continue;
        }
        std::string nm;
        try {
            nm = FStringToUtf8(actor->GetName());
        } catch (...) {
            nm = name;
        }
        if (!nm.empty()) {
            Application::instance().engine().removeActor(nm);
        }
        if (isLiveObject(actor)) {
            if (!actor->Destroy()) {
                actor->LifeSpan = 0.001f;
            }
        }
    }

    for (AActor* light : entry.lights) {
        if (!light) {
            continue;
        }

        std::string nm;
        try {
            nm = FStringToUtf8(light->GetName());
        } catch (...) {
            nm = "";
        }

        if (!nm.empty()) {
            Application::instance().engine().removeActor(nm);
        }

        if (isLiveObject(light)) {
            if (!light->Destroy()) {
                light->LifeSpan = 0.001f;
            }
        }
    }
}

int PrefabManager::findEntryIndexByName(const std::string& name, const std::string& prefabName) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (size_t i = 0; i < prefabEntries.size(); ++i) {
        if (prefabEntries[i].name == name && prefabEntries[i].prefabName == prefabName) {
            return (int)i;
        }
    }
    return -1;
}

void PrefabManager::fixupSelectedActive() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (prefabEntries.empty()) {
        selectedActive = -1;
        return;
    }

    if (selectedActive >= (int)prefabEntries.size()) {
        selectedActive = (int)prefabEntries.size() - 1;
    }

    if (selectedActive < 0) {
        selectedActive = 0;
    }

    for (int s = selectedActive; s < (int)prefabEntries.size(); ++s) {
        if (!prefabEntries[s].actors.empty() && isLiveObject(prefabEntries[s].actors[0])) {
            selectedActive = s;
            return;
        }
    }

    for (int s = selectedActive; s >= 0; --s) {
        if (!prefabEntries[s].actors.empty() && isLiveObject(prefabEntries[s].actors[0])) {
            selectedActive = s;
            return;
        }
    }
    selectedActive = -1;
}

void PrefabManager::updateActivePrefabs() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto& e : prefabEntries) {
        e.lights.erase(std::remove_if(e.lights.begin(), e.lights.end(),
                                      [](AActor* a) {
                                          return !isLiveObject(a);
                                      }),
                       e.lights.end());
    }

    size_t before = prefabEntries.size();
    prefabEntries.erase(std::remove_if(prefabEntries.begin(), prefabEntries.end(),
                                       [](const PrefabEntry& entry) {
                                           if (entry.actors.empty()) {
                                               return true;
                                           }
                                           return !isLiveObject(entry.actors[0]);
                                       }),
                        prefabEntries.end());

    if (prefabEntries.size() != before) {
        Logger->debug("updateActivePrefabs: pruned " + std::to_string(before - prefabEntries.size()) + " entries (actor GC'd/lost)");
    }

    fixupSelectedActive();
}

// "Lights" collapsing header: bulk brightness/radius/color/expansion controls + per-light row
void PrefabManager::renderLightsPanel(const std::vector<PrefabEntry>& entries, int selActive) {
    if (!ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " Attached lights")) {
        return;
    }
    if (selActive < 0 || selActive >= (int)entries.size() || entries[selActive].lights.empty()) {
        return;
    }

    ImGui::Indent();

    ImGui::Text("Lights for %s: %d", entries[selActive].name.c_str(), (int)entries[selActive].lights.size());
    renderLightBulkControls(entries, selActive);
    renderLightExpansionControl();
    for (size_t lightIndex = 0; lightIndex < entries[selActive].lights.size(); ++lightIndex) {
        renderLightRow(entries[selActive].lights[lightIndex], lightIndex);
    }
    ImGui::Unindent();
}

// Brightness/Radius/Color sliders applied to all lights of the selected entry
void PrefabManager::renderLightBulkControls(const std::vector<PrefabEntry>& entries, int selActive) {
    float allB = lightBrightness, allR = lightRadius;
    if (isLiveObject(entries[selActive].lights[0])) {
        ALight* al0 = static_cast<ALight*>(entries[selActive].lights[0]);
        if (isLiveObject(al0) && isLiveObject(al0->LightComponent)) {
            allB = al0->LightComponent->Brightness;
            if (al0->LightComponent->IsA(UPointLightComponent::StaticClass())) {
                allR = static_cast<UPointLightComponent*>(al0->LightComponent)->Radius;
            }
        }
    }
    if (ImGui::DragFloat("Brightness (all)", &allB, 0.1f, 0.0f, 20.0f)) {
        lightBrightness = allB;
        int sa = selActive;
        float b = allB;
        float cr = lightColor[0], cg = lightColor[1], cb = lightColor[2];
        Application::instance().engine().postGameThreadTask([this, sa, b, cr, cg, cb]() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (sa < 0 || sa >= (int)prefabEntries.size()) {
                return;
            }
            for (AActor* l : prefabEntries[sa].lights) {
                if (!isLiveObject(l)) {
                    continue;
                }
                ALight* al = static_cast<ALight*>(l);
                if (!isLiveObject(al) || !isLiveObject(al->LightComponent)) {
                    continue;
                }
                FColor col{(unsigned char)(cb * 255), (unsigned char)(cg * 255), (unsigned char)(cr * 255), 255};
                al->LightComponent->Brightness = b;
                al->LightComponent->SetLightProperties(b, col, al->LightComponent->Function);
                al->LightComponent->bEnabled = 1;
                al->LightComponent->UpdateLightShaftParameters();
            }
            nudgeLights(sa);
        });
    }
    if (ImGui::DragFloat("Radius (all)", &allR, 10.0f, 0.0f, 3000.0f)) {
        lightRadius = allR;
        int sa = selActive;
        float r = allR;
        Application::instance().engine().postGameThreadTask([this, sa, r]() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (sa < 0 || sa >= (int)prefabEntries.size()) {
                return;
            }
            for (AActor* l : prefabEntries[sa].lights) {
                if (!isLiveObject(l)) {
                    continue;
                }
                ALight* al = static_cast<ALight*>(l);
                if (!isLiveObject(al) || !isLiveObject(al->LightComponent)) {
                    continue;
                }
                if (al->LightComponent->IsA(UPointLightComponent::StaticClass())) {
                    static_cast<UPointLightComponent*>(al->LightComponent)->Radius = r;
                }
            }
            nudgeLights(sa);
        });
    }
    if (ImGui::ColorEdit3("Color (all)", lightColor)) {
        int sa = selActive;
        float r = lightColor[0], g = lightColor[1], b2 = lightColor[2];
        Application::instance().engine().postGameThreadTask([this, sa, r, g, b2]() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (sa < 0 || sa >= (int)prefabEntries.size()) {
                return;
            }
            for (AActor* l : prefabEntries[sa].lights) {
                if (!isLiveObject(l)) {
                    continue;
                }
                ALight* al = static_cast<ALight*>(l);
                if (!isLiveObject(al) || !isLiveObject(al->LightComponent)) {
                    continue;
                }
                FColor col{(unsigned char)(b2 * 255), (unsigned char)(g * 255), (unsigned char)(r * 255), 255};
                al->LightComponent->SetLightProperties(al->LightComponent->Brightness, col, al->LightComponent->Function);
            }
            nudgeLights(sa);
        });
    }
}

// Expansion slider; throttled while dragging, final update on release
void PrefabManager::renderLightExpansionControl() {
    float curExp = lightExpansion;
    if (ImGui::DragFloat("Expansion (all)", &curExp, 5.0f, -200.0f, 500.0f)) {
        lightExpansion = curExp;
        if (ImGui::GetTime() - lastExpansionUpdate > 0.25f) {
            lastExpansionUpdate = ImGui::GetTime();
            float offset = lightExpansion;
            Application::instance().engine().postGameThreadTask([this, offset]() {
                updateLightExpansion(offset);
            });
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        float offset = lightExpansion;
        Application::instance().engine().postGameThreadTask([this, offset]() {
            updateLightExpansion(offset);
        });
    }
}

// Single light row: enabled checkbox, name, select, delete
void PrefabManager::renderLightRow(AActor* light, size_t lightIndex) {
    if (!isLiveObject(light)) {
        return;
    }
    ALight* alChk = static_cast<ALight*>(light);
    bool enabled = isLiveObject(alChk) && isLiveObject(alChk->LightComponent) ? alChk->LightComponent->bEnabled != 0 : true;
    if (ImGui::Checkbox(("##lightEnabled" + std::to_string(lightIndex)).c_str(), &enabled)) {
        std::string lName = FStringToUtf8(light->GetName());
        bool newEnabled = enabled;
        Application::instance().engine().postGameThreadTask([this, lName, newEnabled]() {
            int foundEntry = -1;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                for (size_t ei = 0; ei < prefabEntries.size(); ++ei) {
                    for (AActor* lk : prefabEntries[ei].lights) {
                        if (lk && FStringToUtf8(lk->GetName()) == lName) {
                            foundEntry = (int)ei;
                            break;
                        }
                    }
                    if (foundEntry >= 0) {
                        break;
                    }
                }
                if (foundEntry >= 0) {
                    auto& lights = prefabEntries[foundEntry].lights;
                    for (AActor* lk : lights) {
                        if (lk && FStringToUtf8(lk->GetName()) == lName) {
                            ALight* aal = static_cast<ALight*>(lk);
                            if (isLiveObject(aal) && isLiveObject(aal->LightComponent)) {
                                aal->LightComponent->bEnabled = newEnabled ? 1 : 0;
                                aal->LightComponent->UpdateLightShaftParameters();
                            }
                            break;
                        }
                    }
                }
            }
            if (foundEntry >= 0) {
                nudgeLights(foundEntry);
                // disabling should also update material so preview stays correct
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if (foundEntry < (int)prefabEntries.size()) {
                    PrefabEntry& entry = prefabEntries[foundEntry];
                    for (AActor* actor : entry.actors) {
                        if (!isLiveObject(actor)) {
                            continue;
                        }
                        for (int ci = 0; ci < (int)actor->Components.Count(); ++ci) {
                            UObject* co = actor->Components(ci);
                            if (!co || !co->IsA(UMeshComponent::StaticClass())) {
                                continue;
                            }
                            UMeshComponent* comp = static_cast<UMeshComponent*>(co);
                            comp->PrestreamTextures(5.0f, 1, 0);
                        }
                    }
                }
            }
        });
    }
    ImGui::SameLine();
    ImGui::Text("%s", FStringToUtf8(light->GetName()).c_str());
    ImGui::SameLine();
    if (ImGui::Button(("Select##lightSel" + std::to_string(lightIndex)).c_str())) {
        Application::instance().ui().selectActor(light);
    }
    ImGui::SameLine();
    if (ImGui::Button(("Delete##light" + std::to_string(lightIndex)).c_str())) {
        std::string lName = FStringToUtf8(light->GetName());
        Application::instance().engine().postGameThreadTask([this, lName]() {
            // same mechanism as Target menu: Engine::removeActor handles spawnedNames + Destroy
            if (!lName.empty()) {
                Application::instance().engine().removeActor(lName);
            }
            AActor* found = Application::instance().engine().findActorByName(lName);
            if (isLiveObject(found)) {
                if (!found->Destroy()) {
                    found->LifeSpan = 0.001f;
                }
            }
            std::lock_guard<std::recursive_mutex> lock(mutex);
            for (auto& entry : prefabEntries) {
                auto& lights = entry.lights;
                for (size_t k = 0; k < lights.size(); ++k) {
                    AActor* lk = lights[k];
                    if (lk && FStringToUtf8(lk->GetName()) == lName) {
                        // ensure destroyed if still live
                        if (isLiveObject(lk)) {
                            if (!lk->Destroy()) {
                                lk->LifeSpan = 0.001f;
                            }
                        }
                        lights.erase(lights.begin() + k);
                        return;
                    }
                    // also match by pointer if name mismatch
                    if (lk == found) {
                        lights.erase(lights.begin() + k);
                        return;
                    }
                }
            }
        });
    }
}

void PrefabManager::renderAvailablePrefabsPanel() {
    ImGui::Separator();
    ImGui::Text("Available prefabs:");
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##prefab_search", searchFilter, sizeof(searchFilter));
    ImGui::PopItemWidth();
    std::string filterLower = toLowerStr(searchFilter);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##prefab_refresh")) {
        lastCollect = ImGui::GetTime();
        collectPrefabs();
        needsRefresh = false;
    }
    // add selected prefab
    if (!selectedPrefabName.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS "##prefab")) {
            std::string name = selectedPrefabName;
            Application::instance().engine().postGameThreadTask([this, name]() {
                spawnPrefab(name);
            });
        }
    }

    ImGui::Text("Selected prefab:");
    ImGui::SameLine();
    ImGui::Text("%s", selectedPrefabName.empty() ? "(none)" : selectedPrefabName.c_str());
    {
        ChildScope child("##prefab_template_list", ImVec2(0, 180), true);
        if (child.open) {
            // package headers produce variable heights -> clipper would miscount; use plain loop
            std::string lastPkg;
            int shown = 0;
            for (size_t i = 0; i < available.size(); ++i) {
                const PrefabTemplate& pt = available[i];
                if (!filterLower.empty() && pt.nameLower.find(filterLower) == std::string::npos) {
                    continue;
                }
                if (pt.package != lastPkg) {
                    ImColor greyColor = ImColor(0.6f, 0.6f, 0.6f, 1.0f);
                    ImGui::TextColored(greyColor, "%s", pt.package.c_str());
                    ImGui::Separator();
                    lastPkg = pt.package;
                }
                std::string label = pt.name + " (" + std::to_string(pt.archetypeCount) + ")";
                if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), pt.name == selectedPrefabName)) {
                    selectedPrefabName = pt.name;
                }
                ++shown;
            }
            if (shown == 0) {
                ImGui::TextDisabled("(no prefabs match)");
            }
        }
    }
}

void PrefabManager::renderActivePrefabsPanel(const std::vector<PrefabEntry>& entries, int& selActive) {
    ChildScope childActive("##prefab_active_list", ImVec2(0, 200), true);
    if (childActive.open) {
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##prefab_active_refresh")) {
            Application::instance().engine().postGameThreadTask([this]() {
                updateActivePrefabs();
            });
        }

        ImGui::Separator();
        if (entries.empty()) {
            ImGui::TextDisabled("(no active prefabs)");
        } else {
            for (size_t i = 0; i < entries.size(); ++i) {
                const PrefabEntry& entry = entries[i];
                if (entry.actors.empty()) {
                    continue;
                }
                std::ostringstream ss;
                ss << entry.name;
                ImGui::Text("%s", ss.str().c_str());
                ImGui::SameLine();
                if (ImGui::Button((std::string(ICON_FA_CROSSHAIRS "##") + entry.name + std::to_string(i)).c_str())) {
                    {
                        std::lock_guard<std::recursive_mutex> lock(mutex);
                        selectedActive = (int)i;
                    }
                    selActive = (int)i;
                    if (!entry.actors.empty()) {
                        Application::instance().ui().selectActor(entry.actors[0]);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##" + entry.name + std::to_string(i)).c_str())) {
                    std::string nm = entry.name;
                    std::string pn = entry.prefabName;
                    Application::instance().engine().postGameThreadTask([this, nm, pn]() {
                        removePrefabByName(nm, pn);
                    });
                }
            }
        }
    }
}

void PrefabManager::renderMaterialControls() {
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEARS " Force update material")) {
        Application::instance().engine().postGameThreadTask([this]() {
            forceUpdateMaterial();
        });
    }
}

void PrefabManager::renderLightSpawnControls() {
    const char* lightConfigNames[] = {"Corner lights", "Face lights", "Corner and face lights (heavy)"};
    int cfg = (int)selectedLightConfig;
    if (ImGui::Combo("Light config", &cfg, lightConfigNames, 3)) {
        selectedLightConfig = (LightConfig)cfg;
    }
    if (ImGui::Button(ICON_FA_PLUS " Spawn lights")) {
        spawnLightsByConfig(selectedLightConfig);
    }
}

void PrefabManager::renderUI() {
    ZoneScopedN("Prefabs::renderUI");
    if (!ImGui::CollapsingHeader(ICON_FA_BOX_OPEN " Prefabs")) {
        return;
    }

    ImGui::Indent();
    if ((needsRefresh || available.empty()) && ImGui::GetTime() - lastCollect > 1.0f) {
        lastCollect = ImGui::GetTime();
        collectPrefabs();
        needsRefresh = false;
    }

    renderAvailablePrefabsPanel();

    ImGui::Separator();
    ImGui::Text("Active:");
    std::vector<PrefabEntry> entries;
    int selActive;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        entries = prefabEntries;
        selActive = selectedActive;
    }
    renderActivePrefabsPanel(entries, selActive);

    renderMaterialControls();
    renderLightsPanel(entries, selActive);
    renderLightSpawnControls();
    ImGui::Unindent();
}
