#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "engine.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>

#include "logger.h"
#include "util.h"
#include "application.h"

#include <LESDK/Common/Math.hpp>

// This is engine task queue handling. Every time we have something to do on the Engine side,
// we do it here.

void Engine::postGameThreadTask(std::function<void()> fn)
{
    std::lock_guard<std::mutex> lock(gameTasksMutex);
    gameTasks.push_back(std::move(fn));
}

void Engine::drainGameThreadTasks()
{
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(gameTasksMutex);
        if (gameTasks.empty()) {
            return;
        }
        local.swap(gameTasks);
    }
    for (auto& fn : local) {
        fn();
    }
}

static UObject* findFirstObjectOfClass(UClass* cls) {
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (!o) {
            continue;
        }
        if (o->IsA(cls)) {
            return o;
        }
    }
    return nullptr;
}

static APlayerController* findFirstPlayerController() {
    return (APlayerController*)findFirstObjectOfClass(APlayerController::StaticClass());
}

AActor* Engine::findActorByName(const std::string& name)
{
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    std::string needle = toLowerStr(name);

    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* obj = UObject::GObjObjects->GetData()[i];
        if (!obj) {
            continue;
        }
        if (!obj->IsA(AActor::StaticClass())) {
            continue;
        }

        FString fn = obj->GetName();
        std::string s = FStringToUtf8(fn);
        std::string sLower = toLowerStr(s);
        if (needle.empty()) {
            // empty name -> any pawn, spawning needs an actor to spawn from
            // prefer player pawn, fallback to any BioPawn/Pawn
            if (obj->IsA(ASFXPawn_Player::StaticClass()) || obj->IsA(ABioPawn::StaticClass()) || obj->IsA(APawn::StaticClass())) {
                return (AActor*)obj;
            }
            continue;
        }
        if (sLower.find(needle) != std::string::npos) {
            return (AActor*)obj;
        }
    }
    return nullptr;
}

USkeletalMeshComponent* Engine::findPawnMesh(const std::string& pawnName)
{
    AActor* actor = Engine::findActorByName(pawnName);
    if (!actor) {
        return nullptr;
    }
    if (!actor->IsA(APawn::StaticClass())) {
        return nullptr;
    }

    return ((APawn*)actor)->Mesh;
}


void Engine::setPause(bool pause)
{
    if (!UObject::GObjObjects) {
        Logger->debug("setPause: no GObjObjects");
        return;
    }
    
    UGameUISceneClient* client = (UGameUISceneClient*)findFirstObjectOfClass(UGameUISceneClient::StaticClass());
    if (!client) {
        Logger->debug("setPause: UGameUISceneClient not found");
        return;
    }
    client->eventPauseGame(pause ? 1 : 0, 0);

    std::ostringstream ss; ss << "setPause: called PauseGame(" << (pause?"true":"false") << ")";
    Logger->debug(ss.str());
}


AActor* Engine::spawnClass(const std::string& className, const Transform& t)
{
    if (className.empty()) {
        Logger->debug("spawnClass: empty class name");
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
        return nullptr;
    }
    if (cls->ClassFlags & CLASS_Abstract) {
        std::ostringstream ss;
        ss << "spawnClass: class is abstract (ClassFlags=0x" << std::hex << cls->ClassFlags << std::dec << "), cannot spawn";
        Logger->debug(ss.str());
        return nullptr;
    }

    AActor* caller = findActorByName("");
    if (!caller) {
        Logger->debug("spawnClass: no actor available to spawn from (how did this even happen?!)");
        return nullptr;
    }

    FVector loc;
    loc.X = t.pos[0];
    loc.Y = t.pos[1];
    loc.Z = t.pos[2];
    FRotator rot;
    rot.Pitch = DegreesToUnrealRotationUnits(t.rot[0]);
    rot.Yaw   = DegreesToUnrealRotationUnits(t.rot[1]);
    rot.Roll  = DegreesToUnrealRotationUnits(t.rot[2]);

    AActor* spawned = caller->Spawn(cls, NULL, SFXName(), loc, rot, NULL, NULL, 1, 0);
    if (!spawned) {
        Logger->debug("spawnClass: spawn returned null, dumping class diagnostics:");
        Logger->debug(diagnoseClass(className));
        return nullptr;
    }

    std::string nm = FStringToUtf8(spawned->GetName());
    if (std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), nm) == spawnedNamesVector.end()) {
        spawnedNamesVector.push_back(nm);
    }

    std::ostringstream ss;
    ss  << "spawnClass: spawned '" << className << "' as '" << nm
        << "' at (" << t.pos[0] << "," << t.pos[1] << "," << t.pos[2] << ")";
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
        ss << "  Spawn caller: " << FStringToUtf8(caller->GetName())
           << " (outer=" << (caller->Outer ? FStringToUtf8(caller->Outer->GetName()) : "(none)") << ")\n";
    }
    return ss.str();
}

void Engine::removeActor(const std::string& name)
{
    if (name.empty()) {
        Logger->debug("removeActor: empty name");
        return;
    }

    AActor* actor = Engine::findActorByName(name);
    if (!actor) {
        Logger->debug("removeActor: target not found");
        return;
    }

    //TODO: make this emit a notification (toast) via TyomaVader/ImGuiNotify
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

void Engine::loadTransformFromPawn(const std::string& pawnName, Transform& t)
{
    AActor* actor = findActorByName(pawnName);
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

void Engine::setTransform(const std::string& targetName, const Transform& t)
{
    AActor* actor = Engine::findActorByName(targetName);
    if (!actor) {
        Logger->debug("setTransform: target not found");
        return;
    }

    FVector loc;
    loc.X = t.pos[0];
    loc.Y = t.pos[1];
    loc.Z = t.pos[2];
    FRotator rot;
    rot.Pitch = DegreesToUnrealRotationUnits(t.rot[0]);
    rot.Yaw   = DegreesToUnrealRotationUnits(t.rot[1]);
    rot.Roll  = DegreesToUnrealRotationUnits(t.rot[2]);

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
    ss  << "Game_SetTransform: target='" << FStringToUtf8(actor->GetName()) << "' locOk=" << locOk
        << " loc=(" << actor->Location.X << "," << actor->Location.Y << "," << actor->Location.Z << ")"
        << " phys=" << (int)oldPhys << " scaleOk=" << (scaleOk ? "true" : "false");
    Logger->debug(ss.str());
}

// suspend collision for floating
void Engine::setFloat(const std::string& targetName, bool enable)
{
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
    if (UObject::GObjObjects) {
        for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
            UObject* o = UObject::GObjObjects->GetData()[i];
            if (!o) {
                continue;
            }
            if (!o->IsA(UPrimitiveComponent::StaticClass())) {
                continue;
            }
            
            UPrimitiveComponent* c = (UPrimitiveComponent*) o;
            if (c->Outer == actor) {
                comps.push_back(c);
            }
        }
    }
    for (UPrimitiveComponent* c : comps) {
        c->SetBlockRigidBody(enable ? 0 : 1);
    }

    std::ostringstream ss;
    ss  << "setFloat: '" << targetName << "' enable=" << (enable ? "true" : "false")
        << " oldPhys=" << (int)floatOldPhysics
        << " collide=" << (int)actor->bCollideActors
        << " world=" << (int)actor->bCollideWorld
        << " blockActors=" << (int)actor->bBlockActors
        << " blockRigid=" << (int)actor->BlockRigidBody
        << " comps=" << comps.size();
    Logger->debug(ss.str());
}

void Engine::applyHUDVisibility() {
    AHUD* hud = (AHUD*)findFirstObjectOfClass(ABioHUD::StaticClass());
    if (!hud) {
        return;
    }

    if (isGameUIHiddenState) {
        if (!hudShowBitsSaved) {
            savedHUDShowBits = hud->bShowHUD;
            hudShowBitsSaved = true;
        }
        hud->bShowHUD = false;
    } else if (hudShowBitsSaved) {
        hud->bShowHUD = savedHUDShowBits;
        hudShowBitsSaved = false;
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
            pi->aMouseX = 0.0f; pi->aMouseY = 0.0f; pi->aForward = 0.0f;
            pi->aTurn = 0.0f; pi->aStrafe = 0.0f; pi->aUp = 0.0f; pi->aLookUp = 0.0f;
            pi->SmoothedMouse[0] = 0.0f;
            pi->SmoothedMouse[1] = 0.0f;
        }
    } else if (lookFrozen) {
        pc->bIgnoreLookInput = savedIgnoreLook;
        pc->bIgnoreMoveInput = savedIgnoreMove;
        lookFrozen = false;
    }
}




