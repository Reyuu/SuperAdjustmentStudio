#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "engine.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>

#include "application.h"
#include "logger.h"
#include "util.h"

#include <LESDK/Common/Math.hpp>

// UGameEngine::Tick
using gameEngineTickType = void(void*, float);
static gameEngineTickType* origGameEngineTick = nullptr;

static void hkGameEngineTick(void* self, float dt) {
    SAS_HOOK_TRY {
        if (origGameEngineTick) {
            origGameEngineTick(self, dt);
        }
        Application::instance().engine().drainPackageLoads();
        Application::instance().engine().drainGameThreadTasks();

        static UWorld* s_lastWorld = nullptr;
        UWorld* curWorld = (GWorld && *GWorld) ? *GWorld : nullptr;
        if (curWorld != s_lastWorld) {
            s_lastWorld = curWorld;
            if (curWorld) {
                Application::instance().particles().removeAllParticles();
            }
        }
    } SAS_HOOK_CATCH_VOID
}

void Engine::initTickHook(HookManager& hookManager, SDKContext& sdk) {
    void* tickAddress = sdk.gameEngineTickAddress();
    if (tickAddress && sdk.initializer()) {
        origGameEngineTick = (gameEngineTickType*)hookManager.install("GameEngineTick", tickAddress, &hkGameEngineTick);
    }
}

// This is engine task queue handling. Every time we have something to do on the Engine side,
// we do it here.

void Engine::postGameThreadTask(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(gameTasksMutex);
    gameTasks.push_back(std::move(fn));
}

void Engine::drainGameThreadTasks() {
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(gameTasksMutex);
        if (gameTasks.empty()) {
            return;
        }
        local.swap(gameTasks);
    }
    for (auto& fn : local) {
        if (fn) {
            fn();
        }
    }
}

void Engine::postPackageLoad(const std::string& package, std::function<void()> onLoaded) {
    std::lock_guard<std::mutex> lock(loadTasksMutex);
    loadTasks.push_back({package, std::move(onLoaded)});
}

void Engine::drainPackageLoads() {
    std::vector<PackageLoadTask> local;
    {
        std::lock_guard<std::mutex> lock(loadTasksMutex);
        if (loadTasks.empty()) {
            return;
        }
        local.swap(loadTasks);
    }
    for (auto& task : local) {
        loadPackageType* lp = Application::instance().sdk().loadPackage();
        if (!lp) {
            Logger->debug("drainPackageLoads: loadPackageFn not initialized, dropping '" + task.package + "'");
            continue;
        }
        std::wstring pkgW = toWString(task.package);
        Logger->debug("drainPackageLoads: loading package '" + task.package + "'...");
        UPackage* upkg = lp(nullptr, pkgW.data(), LOAD_NoWarn);
        if (!upkg) {
            Logger->debug("drainPackageLoads: package load FAILED '" + task.package + "'");
            Logger->dump_backtrace();
            continue;
        }
        Logger->debug("drainPackageLoads: package loaded " + FStringToUtf8(upkg->GetName()));
        if (task.onLoaded) {
            task.onLoaded();
        }
    }
}

static APlayerController* findFirstPlayerController() {
    return findFirstOf<APlayerController>();
}

AActor* Engine::findActorByName(const std::string& name) {
    std::string needle = toLowerStr(name);
    AActor* found = nullptr;
    AActor* fallback = nullptr;

    forEachOf<AActor>([&](AActor* obj) {
        if (found) {
            return;
        }
        FString fn = obj->GetName();
        std::string s = FStringToUtf8(fn);
        std::string sLower = toLowerStr(s);
        if (needle.empty()) {
            // empty name -> any pawn, spawning needs an actor to spawn from
            // prefer a real in-world pawn
            if (obj->IsA(ASFXPawn_Player::StaticClass()) || obj->IsA(ABioPawn::StaticClass()) || obj->IsA(APawn::StaticClass())) {
                if (sLower.rfind("default__", 0) == 0) {
                    if (!fallback) {
                        fallback = obj;
                    }
                } else {
                    found = obj;
                }
            }
            return;
        }
        if (sLower.find(needle) != std::string::npos) {
            found = obj;
        }
    });
    return found ? found : fallback;
}

AActor* Engine::playerPawn() {
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    AActor* fallback = nullptr;
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (!o || !o->IsA(APlayerController::StaticClass())) {
            continue;
        }
        std::string nm = FStringToUtf8(o->GetName());
        if (nm.rfind("Default__", 0) == 0) {
            continue; // skip class default objects (no pawn, not the live player)
        }
        APlayerController* pc = static_cast<APlayerController*>(o);
        if (!pc->Pawn) {
            continue;
        }
        if (pc->IsLocalPlayerController()) {
            return pc->Pawn;
        }
        if (!fallback) {
            fallback = pc->Pawn;
        }
    }
    return fallback;
}

USkeletalMeshComponent* Engine::findPawnMesh(const std::string& pawnName) {
    AActor* actor = Engine::findActorByName(pawnName);
    if (!actor) {
        return nullptr;
    }
    if (!actor->IsA(APawn::StaticClass())) {
        return nullptr;
    }

    return ((APawn*)actor)->Mesh;
}

void Engine::setPause(bool pause) {
    APlayerController* playerController = findFirstPlayerController();
    if (!playerController) {
        Logger->debug("setPause: no player controller");
        return;
    }
    AWorldInfo* worldInfo = playerController->WorldInfo;
    if (!worldInfo) {
        Logger->debug("setPause: no WorldInfo");
        return;
    }
    if (pause && !playerController->PlayerReplicationInfo) {
        Logger->debug("setPause: no player replication info, cannot pause");
        return;
    }
    worldInfo->Pauser = pause ? playerController->PlayerReplicationInfo : nullptr;
    std::ostringstream ss;
    ss << "setPause: " << (pause ? "paused" : "resumed");
    Logger->debug(ss.str());
}

AActor* Engine::spawnClass(const std::string& className, const Transform& t) {
    if (className.empty()) {
        Logger->debug("spawnClass: empty class name");
        Application::instance().ui().toastManager.addToastNotification("Spawn failed: empty class name", ToastTypeError, 3.0);
        return nullptr;
    }

    std::wstring wName = toWString(className);
    std::wstring full = (wName.find(L"Class ") == std::wstring::npos) ? (L"Class " + wName) : wName;
    UClass* cls = UObject::FindClass(full.c_str());
    if (!cls) {
        cls = UObject::FindClass(wName.c_str());
    }
    if (!cls) {
        Logger->debug("spawnClass: class not found");
        Application::instance().ui().toastManager.addToastNotification("Spawn failed: class not found: " + className, ToastTypeError, 3.0);
        return nullptr;
    }
    if (cls->ClassFlags & CLASS_Abstract) {
        std::ostringstream ss;
        ss << "spawnClass: class is abstract (ClassFlags=0x" << std::hex << cls->ClassFlags << std::dec << "), cannot spawn";
        Logger->debug(ss.str());
        Application::instance().ui().toastManager.addToastNotification("Spawn failed: class is abstract: " + className, ToastTypeError, 3.0);
        return nullptr;
    }

    AActor* caller = findActorByName("");
    if (!caller) {
        Logger->debug("spawnClass: no actor available to spawn from (how did this even happen?!)");
        Application::instance().ui().toastManager.addToastNotification("Spawn failed: no actor available to spawn from", ToastTypeError, 3.0);
        return nullptr;
    }

    FVector loc;
    loc.X = t.pos[0];
    loc.Y = t.pos[1];
    loc.Z = t.pos[2];
    FRotator rot;
    rot.Pitch = DegreesToUnrealRotationUnits(t.rot[0]);
    rot.Yaw = DegreesToUnrealRotationUnits(t.rot[1]);
    rot.Roll = DegreesToUnrealRotationUnits(t.rot[2]);

    // ME2's runtime Spawn enforces the Placeable class flag (lights marked non-placeable
    // return NULL). Temporarily set CLASS_Placeable + clear CLASS_NotPlaceable, then
    // restore the original flags right after Spawn returns.
    const DWORD originalFlags = cls->ClassFlags;
    cls->ClassFlags |= CLASS_Placeable;
    cls->ClassFlags &= ~CLASS_NotPlaceable;
    AActor* spawned = caller->Spawn(cls, NULL, SFXName(), loc, rot, NULL, NULL, 1, 0);
    cls->ClassFlags = originalFlags;
    if (!spawned) {
        Logger->debug("spawnClass: spawn returned null, dumping class diagnostics:");
        Logger->debug(diagnoseClass(className));
        Application::instance().ui().toastManager.addToastNotification("Spawn failed: " + className + " returned null (see log)", ToastTypeError, 4.0);
        return nullptr;
    }

    std::string nm = FStringToUtf8(spawned->GetName());
    if (std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), nm) == spawnedNamesVector.end()) {
        spawnedNamesVector.push_back(nm);
    }

    std::ostringstream ss;
    ss << "spawnClass: spawned '" << className << "' as '" << nm << "' at (" << t.pos[0] << "," << t.pos[1] << "," << t.pos[2] << ") via caller '"
       << FStringToUtf8(caller->GetName()) << "'";
    Logger->debug(ss.str());
    return spawned;
}

std::string Engine::diagnoseClass(const std::string& className) {
    std::ostringstream ss;
    std::wstring wName = toWString(className);
    std::wstring full = (wName.find(L"Class ") == std::wstring::npos) ? (L"Class " + wName) : wName;
    UClass* cls = UObject::FindClass(full.c_str());
    if (!cls) {
        cls = UObject::FindClass(wName.c_str());
    }
    if (!cls) {
        ss << "'" << className << "' not found in GObjObjects";
        return ss.str();
    }

    ss << "'" << className << "'" << "\n";
    ss << "  ClassFlags: 0x" << std::hex << cls->ClassFlags << std::dec << "\n";
    ss << "  Abstract: " << ((cls->ClassFlags & CLASS_Abstract) ? "YES -> SpawnActor returns NULL" : "no") << "\n";
    ss << "  Placeable: " << ((cls->ClassFlags & CLASS_Placeable) ? "yes" : "no") << "\n";
    ss << "  NotPlaceable: " << ((cls->ClassFlags & CLASS_NotPlaceable) ? "yes (runtime Spawn still allowed)" : "no") << "\n";
    ss << "  Deprecated: " << ((cls->ClassFlags & CLASS_Deprecated) ? "yes" : "no") << "\n";
    ss << "  Hidden: " << ((cls->ClassFlags & CLASS_Hidden) ? "yes" : "no") << "\n";
    ss << "  ClassDefaultObject: " << (cls->ClassDefaultObject ? "present" : "NULL -> cannot spawn") << "\n";
    if (cls->ClassWithin) {
        ss << "  ClassWithin: " << FStringToUtf8(cls->ClassWithin->GetName()) << " (outer must match)\n";
    } else {
        ss << "  ClassWithin: (none)\n";
    }

    AActor* caller = findActorByName("");
    if (!caller) {
        ss << "  Spawn caller: NONE -> nothing to Spawn() from\n";
    } else {
        ss << "  Spawn caller: " << FStringToUtf8(caller->GetName()) << " (outer=" << (caller->Outer ? FStringToUtf8(caller->Outer->GetName()) : "(none)")
           << ")\n";
    }
    return ss.str();
}

void Engine::removeActor(const std::string& name) {
    if (name.empty()) {
        Logger->debug("removeActor: empty name");
        return;
    }

    AActor* actor = Engine::findActorByName(name);
    if (!actor) {
        Logger->debug("removeActor: target not found");
        return;
    }

    // TODO: make this emit a notification (toast) via TyomaVader/ImGuiNotify
    if (actor->IsA(APawn::StaticClass())) {
        Logger->debug("removeActor: refusing to delete pawn '" + name + "'");
        return;
    }
    APlayerController* pc = findFirstPlayerController();
    if (pc && pc->Pawn == actor) {
        Logger->debug("removeActor: refusing to delete the player controller's pawn '" + name + "'");
        return;
    }

    std::string nm = FStringToUtf8(actor->GetName());
    bool ok = actor->Destroy();

    auto it = std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), name);
    if (it != spawnedNamesVector.end()) {
        spawnedNamesVector.erase(it);
    }
    it = std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), nm);
    if (it != spawnedNamesVector.end()) {
        spawnedNamesVector.erase(it);
    }

    std::ostringstream ss;
    ss << "removeActor: '" << nm << "' destroyed=" << (ok ? "true" : "false");
    Logger->debug(ss.str());
    Application::instance().ui().collectPawns();
}

void Engine::loadTransformFromActor(AActor* actor, Transform& t) {
    if (!actor) {
        return;
    }
    t.pos[0] = actor->Location.X;
    t.pos[1] = actor->Location.Y;
    t.pos[2] = actor->Location.Z;
    t.rot[0] = UnrealRotationUnitsToDegrees(actor->Rotation.Pitch);
    t.rot[1] = UnrealRotationUnitsToDegrees(actor->Rotation.Yaw);
    t.rot[2] = UnrealRotationUnitsToDegrees(actor->Rotation.Roll);
    t.scale[0] = actor->DrawScale3D.X;
    t.scale[1] = actor->DrawScale3D.Y;
    t.scale[2] = actor->DrawScale3D.Z;
}

void Engine::loadTransformFromPawn(const std::string& pawnName, Transform& t) {
    loadTransformFromActor(findActorByName(pawnName), t);
}

void Engine::setTransform(AActor* actor, const Transform& t) {
    if (!actor) {
        Logger->debug("setTransform: actor is null");
        return;
    }

    FVector loc;
    loc.X = t.pos[0];
    loc.Y = t.pos[1];
    loc.Z = t.pos[2];
    FRotator rot;
    rot.Pitch = DegreesToUnrealRotationUnits(t.rot[0]);
    rot.Yaw = DegreesToUnrealRotationUnits(t.rot[1]);
    rot.Roll = DegreesToUnrealRotationUnits(t.rot[2]);

    unsigned char oldPhys = actor->Physics;
    if (oldPhys != 0) {
        actor->Physics = 0;
    }

    bool locOk = actor->SetLocation(loc);
    actor->SetRotation(rot);

    actor->Physics = oldPhys;

    bool scaleOk = false;
    USkeletalMeshComponent* mesh = nullptr;
    if (actor->IsA(APawn::StaticClass())) {
        mesh = ((APawn*)actor)->Mesh;
    }

    FVector s;
    s.X = t.scale[0];
    s.Y = t.scale[1];
    s.Z = t.scale[2];
    if (mesh) {
        mesh->SetScale3D(s);
        scaleOk = true;
    } else {
        actor->SetDrawScale3D(s);
        scaleOk = true;
    }

    std::ostringstream ss;
    ss << "Game_SetTransform: target='" << FStringToUtf8(actor->GetName()) << "' locOk=" << locOk << " loc=(" << actor->Location.X << "," << actor->Location.Y
       << "," << actor->Location.Z << ")"
       << " phys=" << (int)oldPhys << " scaleOk=" << (scaleOk ? "true" : "false");
    Logger->debug(ss.str());
}

void Engine::setTransform(const std::string& targetName, const Transform& t) {
    setTransform(findActorByName(targetName), t);
}

// suspend collision for floating
void Engine::setFloat(const std::string& targetName, bool enable) {
    AActor* actor = findActorByName(targetName);
    if (!actor) {
        Logger->debug("setFloat: target not found");
        return;
    }

    // enable floating (disable collisions)
    if (enable) {
        floatOldPhysics = actor->Physics;
        floatOldCollideActors = actor->bCollideActors;
        floatOldCollideWorld = actor->bCollideWorld;
        floatOldBlockActors = actor->bBlockActors;
        floatOldBlockRigidBody = actor->BlockRigidBody;
        actor->Physics = 0; // PHYS_None
        actor->bCollideActors = false;
        actor->bCollideWorld = false;
        actor->bBlockActors = false;
        actor->BlockRigidBody = false;
    } else { // disable floating (enable collisions)
        actor->Physics = floatOldPhysics;
        actor->bCollideActors = floatOldCollideActors;
        actor->bCollideWorld = floatOldCollideWorld;
        actor->bBlockActors = floatOldBlockActors;
        actor->BlockRigidBody = floatOldBlockRigidBody;
    }

    // rigid body blocking
    std::vector<UPrimitiveComponent*> comps;
    if (actor->CollisionComponent) {
        comps.push_back(actor->CollisionComponent);
    }
    if (actor->IsA(APawn::StaticClass()) && ((APawn*)actor)->Mesh) {
        comps.push_back(((APawn*)actor)->Mesh);
    }
    forEachOf<UPrimitiveComponent>([&](UPrimitiveComponent* c) {
        if (c->Outer == actor) {
            comps.push_back(c);
        }
    });
    for (UPrimitiveComponent* c : comps) {
        c->SetBlockRigidBody(enable ? 0 : 1);
    }

    std::ostringstream ss;
    ss << "setFloat: '" << targetName << "' enable=" << (enable ? "true" : "false") << " oldPhys=" << (int)floatOldPhysics
       << " collide=" << (int)actor->bCollideActors << " world=" << (int)actor->bCollideWorld << " blockActors=" << (int)actor->bBlockActors
       << " blockRigid=" << (int)actor->BlockRigidBody << " comps=" << comps.size();
    Logger->debug(ss.str());
}

void Engine::applyHUDVisibility() {
    if (isGameUIHiddenState) {
        forEachOf<UBioSFPanel>([this](UBioSFPanel* p) {
            if (savedPanelVisibility.find(p) == savedPanelVisibility.end()) {
                savedPanelVisibility.emplace(p, p->IsVisible != 0);
            }
            p->SetMovieVisibility(false);
        });
    } else {
        for (auto it = savedPanelVisibility.begin(); it != savedPanelVisibility.end();) {
            if (isLiveObject(it->first)) {
                it->first->SetMovieVisibility(it->second ? true : false);
            }
            it = savedPanelVisibility.erase(it);
        }
    }
}

void Engine::freezeLook(bool freeze) {
    APlayerController* pc = findFirstPlayerController();
    if (!pc) {
        return;
    }
    if (freeze) {
        if (!lookFrozen) {
            savedIgnoreLook = pc->bIgnoreLookInput;
            savedIgnoreMove = pc->bIgnoreMoveInput;
            lookFrozen = true;
        }
        pc->bIgnoreLookInput = true;
        pc->bIgnoreMoveInput = true;
        if (pc->PlayerInput) {
            UPlayerInput* pi = pc->PlayerInput;
            pi->aMouseX = 0.0f;
            pi->aMouseY = 0.0f;
            pi->aForward = 0.0f;
            pi->aTurn = 0.0f;
            pi->aStrafe = 0.0f;
            pi->aUp = 0.0f;
            pi->aLookUp = 0.0f;
            pi->SmoothedMouse[0] = 0.0f;
            pi->SmoothedMouse[1] = 0.0f;
        }
    } else if (lookFrozen) {
        pc->bIgnoreLookInput = savedIgnoreLook;
        pc->bIgnoreMoveInput = savedIgnoreMove;
        lookFrozen = false;
    }
}
