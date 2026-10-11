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
#include <LESDK/Includes.hpp>

#include "tracy.h"

// UGameEngine::Tick
using gameEngineTickType = void(void*, float);
static gameEngineTickType* origGameEngineTick = nullptr;

using engineExecType = unsigned(void*, wchar_t*, void*);
static engineExecType* origEngineExec = nullptr;

// from LExSDK, it only has a single virtual (Exec at slot 0)
constexpr ptrdiff_t VIEWPORT_FEXEC_OFFSET = 0x68;

static void hkGameEngineTick(void* self, float dt) {
    SAS_HOOK_TRY {
        if (origGameEngineTick) {
            origGameEngineTick(self, dt);
        }
        Application::instance().engine().drainPackageLoads();
        Application::instance().engine().drainGameThreadTasks();
#ifdef SDK_TARGET_LE1
        // the game resets head gear every tick, re-assert on the game thread too
        Application::instance().npcs().applyHeadGearOverrides();
#endif
        Application::instance().freecam().assertFreecamCache();

        static UWorld* s_lastWorld = nullptr;
        UWorld* curWorld = (GWorld && *GWorld) ? *GWorld : nullptr;
        if (curWorld != s_lastWorld) {
            s_lastWorld = curWorld;
            if (curWorld) {
                Application::instance().particles().removeAllParticles();
                Application::instance().freecam().resetFreecamState();
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

// one throwing task must not take the rest of the batch down with it: the batch
// is a local swap, so an exception escaping the loop would silently discard every
// task queued behind it (and skip the rest of the tick hook)
static void runTaskGuarded(const std::function<void()>& fn) {
    try {
        if (fn) {
            fn();
        }
    } catch (const std::exception& e) {
        Logger->error("Engine task threw: {}", e.what());
    } catch (...) {
        Logger->error("Engine task threw: unknown (non-std)");
    }
}

void Engine::postGameThreadTask(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(gameTasksMutex);
    gameTasks.push_back(std::move(fn));
}

void Engine::drainGameThreadTasks() {
    ZoneScopedN("Engine::drainGameThreadTasks");
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(gameTasksMutex);
        if (gameTasks.empty()) {
            return;
        }
        local.swap(gameTasks);
    }
    for (auto& fn : local) {
        runTaskGuarded(fn);
    }
}

void Engine::consoleCommand(const std::string& command) {
    postGameThreadTask([command]() {
        UEngine* engine = GEngine ? *GEngine : nullptr;
        if (!engine || !engine->GameViewport) {
            Logger->error("consoleCommand: GEngine/GameViewport not initialized");
            return;
        }

        FString cmd;
        cmd.AppendUtf8(command.c_str());
        const wchar_t* chars = cmd.Chars();
        std::vector<wchar_t> buf(chars, chars + cmd.Length() + 1);
        void* outDevice = (GError && *GError) ? *GError : nullptr;

        UGameViewportClient* viewport = engine->GameViewport;
        void* fexec = (char*)viewport + VIEWPORT_FEXEC_OFFSET;
        // get the virtual function table of the fexec object
        void** vft = *(void***)fexec;
        // get the first function in the virtual function table
        engineExecType* viewportExec = (engineExecType*)vft[0];
        unsigned result = viewportExec(fexec, buf.data(), outDevice);
        Logger->debug("consoleCommand: executed command '" + command + "' with result " + std::to_string(result));

        if (result == 0) {
            Logger->debug("consoleCommand: command '" + command + "' failed with result " + std::to_string(result));
        }
    });
}

void Engine::postPackageLoad(const std::string& package, std::function<void()> onLoaded) {
    std::lock_guard<std::mutex> lock(loadTasksMutex);
    loadTasks.push_back({package, std::move(onLoaded)});
}

void Engine::drainPackageLoads() {
    ZoneScopedN("Engine::drainPackageLoads");
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
        // to keep it alive and prevent garbage collection
        // RF_RootSet = 0x4 is exactly what UObject::AddToRoot() does in this engine (UnObj.cpp)
        // but... it still gets GC'd if nothing else references it
        upkg->ObjectFlags |= 0x4ull;
        Logger->debug("drainPackageLoads: package loaded " + FStringToUtf8(upkg->GetName()) + " rooted=1 (ObjectFlags=0x" + [&]() -> std::string {
            std::ostringstream h;
            h << std::hex << upkg->ObjectFlags;
            return h.str();
        }() + ")");
        if (task.onLoaded) {
            runTaskGuarded(task.onLoaded);
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

AActor* Engine::findActorByExactName(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    AActor* found = nullptr;
    forEachOf<AActor>([&](AActor* obj) {
        if (found) {
            return;
        }
        if (toLowerStr(FStringToUtf8(obj->GetName())) == toLowerStr(name)) {
            found = obj;
        }
    });
    return found;
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

    APawn* pawn = static_cast<APawn*>(actor);
    if (pawn->Mesh) {
        return pawn->Mesh;
    }
#ifdef SDK_TARGET_LE1
    // LE1 keeps the skinned meshes in m_aoMeshes and often leaves APawn::Mesh
    // unset, which silently broke animations and bone posing
    if (actor->IsA(ABioPawn::StaticClass())) {
        ABioPawn* bioPawn = static_cast<ABioPawn*>(actor);
        for (int i = 0; i < (int)bioPawn->m_aoMeshes.Count(); ++i) {
            USkeletalMeshComponent* mesh = bioPawn->m_aoMeshes.GetData()[i];
            if (mesh) {
                return mesh;
            }
        }
    }
#endif
    return nullptr;
}

void Engine::setPause(bool pause) {
    // flip bPlayersOnly directly
    constexpr uintptr_t playersOnlyOffsetConst = 0x7C0; // AWorldInfo bitfield in all three games
    constexpr uint32_t playersOnlyConst = 0x400u;
    constexpr uint32_t playersOnlyPendingConst = 0x800u;
    postGameThreadTask([this, pause]() {
        AWorldInfo* wi = nullptr;
        forEachOf<AWorldInfo>([&](AWorldInfo* w) {
            if (wi || !w) {
                return;
            }
            if (FStringToUtf8(w->GetName()).rfind("Default__", 0) == 0) {
                return; // class default object
            }
            wi = w;
        });
        if (!wi) {
            consoleCommand(pause ? "playersonly 1" : "playersonly 0");
            return;
        }
        auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(wi) + playersOnlyOffsetConst);
        if (pause) {
            *flags |= playersOnlyConst | playersOnlyPendingConst;
        } else {
            *flags &= ~(playersOnlyConst | playersOnlyPendingConst);
        }
    });
}

AActor* Engine::spawnClass(const std::string& className, const Transform& t) {
    if (className.empty()) {
        Logger->debug("spawnClass: empty class name");
        Application::instance().ui().toastManager.addToastNotification(t("ui.spawn_table.failed_empty_class_name"), ToastTypeError, 3.0);
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
        char notFoundBuf[512];
        snprintf(notFoundBuf, sizeof(notFoundBuf), t("ui.spawn_table.failed_class_not_found"), className.c_str());
        Application::instance().ui().toastManager.addToastNotification(notFoundBuf, ToastTypeError, 3.0);
        return nullptr;
    }
    if (cls->ClassFlags & CLASS_Abstract) {
        std::ostringstream ss;
        ss << "spawnClass: class is abstract (ClassFlags=0x" << std::hex << cls->ClassFlags << std::dec << "), cannot spawn";
        Logger->debug(ss.str());
        char abstractBuf[512];
        snprintf(abstractBuf, sizeof(abstractBuf), t("ui.spawn_table.failed_class_abstract"), className.c_str());
        Application::instance().ui().toastManager.addToastNotification(abstractBuf, ToastTypeError, 3.0);
        return nullptr;
    }

    AActor* caller = findActorByName("");
    if (!caller) {
        Logger->debug("spawnClass: no actor available to spawn from (how did this even happen?!)");
        Application::instance().ui().toastManager.addToastNotification(t("ui.spawn_table.failed_no_actor_available"), ToastTypeError, 3.0);
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

    // spawn with the flags untouched first. LE2 enforces the Placeable flag, so a
    // refusal retries with it forced. LE1's values are unverified (util.h) and a
    // wrong bit mislabels the class for good, so LE1 never pokes them
    const DWORD originalFlags = cls->ClassFlags;
    auto spawnAttempt = [&](DWORD flags) -> AActor* {
        cls->ClassFlags = flags;
#ifdef SDK_TARGET_LE3
        AActor* res = caller->Spawn(cls, NULL, SFXName(), loc, rot, NULL, 1, 0);
#else
        AActor* res = caller->Spawn(cls, NULL, SFXName(), loc, rot, NULL, NULL, 1, 0);
#endif
        cls->ClassFlags = originalFlags;
        return res;
    };
    AActor* spawned = spawnAttempt(originalFlags);
#ifndef SDK_TARGET_LE1
    if (!spawned) {
        Logger->debug("spawnClass: '" + className + "' refused with its own flags, retrying with CLASS_Placeable forced");
        spawned = spawnAttempt((originalFlags | CLASS_Placeable) & ~CLASS_NotPlaceable);
    }
#endif
    if (!spawned) {
        Logger->debug("spawnClass: spawn returned null, dumping class diagnostics:");
        Logger->debug(diagnoseClass(className));
        char nullBuf[512];
        snprintf(nullBuf, sizeof(nullBuf), t("ui.spawn_table.failed_ret_null_class"), className.c_str());
        Application::instance().ui().toastManager.addToastNotification(nullBuf, ToastTypeError, 4.0);
        return nullptr;
    }

    std::string nm = FStringToUtf8(spawned->GetName());
    addSpawnedName(nm);

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
    // pawns are only removable when we spawned them ourselves, the level's own
    // pawns (and the player) would break the game
    if (actor->IsA(APawn::StaticClass()) && !isSpawnedName(name) && !isSpawnedName(FStringToUtf8(actor->GetName()))) {
        Logger->debug("removeActor: refusing to delete the pawn '" + name + "' (not spawned by SAS)");
        return;
    }
    APlayerController* pc = findFirstPlayerController();
    if (pc && pc->Pawn == actor) {
        Logger->debug("removeActor: refusing to delete the player controller's pawn '" + name + "'");
        return;
    }

    std::string nm = FStringToUtf8(actor->GetName());

    // a duplicated name makes this ambiguous and the wrong actor would die with it,
    // so refuse rather than guess
    int sameName = 0;
    AActor* firstMatch = nullptr;
    forEachOf<AActor>([&](AActor* other) {
        if (!other || FStringToUtf8(other->GetName()) != nm) {
            return;
        }
        ++sameName;
        if (!firstMatch) {
            firstMatch = other;
        }
    });
    if (sameName > 1) {
        std::ostringstream dup;
        dup << "removeActor: '" << nm << "' is ambiguous - " << sameName << " actors share that name, refusing to destroy";
        Logger->debug(dup.str());
        if (firstMatch != actor) {
            Application::instance().ui().toastManager.addToastNotification(t("ui.npcs_table.failed_ambiguous_name"), ToastTypeError, 4.0);
            return;
        }
    }

    // pawns are commonly bNoDelete and the game re-asserts it, which makes Destroy()
    // a no-op, so clear it for the call and put it back after
    const bool savedNoDelete = actor->bNoDelete != 0;
    actor->bNoDelete = 0;
    bool ok = actor->Destroy();
    if (!ok && isLiveObject(actor)) {
        actor->bNoDelete = 0;
        ok = actor->Destroy();
    }
    if (!ok && isLiveObject(actor) && savedNoDelete) {
        actor->bNoDelete = 1;
    }

    removeSpawnedName(name);
    removeSpawnedName(nm);

    std::ostringstream ss;
    ss << "removeActor: '" << nm << "' destroyed=" << (ok ? "true" : "false");
    Logger->debug(ss.str());
    Application::instance().ui().collectPawns();
}

void Engine::loadTransformFromActor(AActor* actor, Transform& t) {
    if (!actor) {
        return;
    }
    t.pos[0] = actor->LOCATION.X;
    t.pos[1] = actor->LOCATION.Y;
    t.pos[2] = actor->LOCATION.Z;
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
    // keep material visible after move (was disappearing)
    Application::instance().prefabs().onActorMoved(actor);

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
    ss << "Game_SetTransform: target='" << FStringToUtf8(actor->GetName()) << "' locOk=" << locOk << " loc=(" << actor->LOCATION.X << "," << actor->LOCATION.Y
       << "," << actor->LOCATION.Z << ")"
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
    if (!isGameUIHiddenState && savedPanelVisibility.empty() && savedModeVisibility.empty() && savedHudVisibility.empty() && savedPoiCompHidden.empty()
#ifndef SDK_TARGET_LE1
        && savedPoiHidden.empty() && savedFlareActive.empty() && savedSelectionTargetable.empty()
#else
        && savedHudExtraFlags.empty() && savedSelectionsInRange.empty()
#endif
    ) {
        return;
    }
    if (isGameUIHiddenState) {
#ifdef SDK_TARGET_LE3
        forEachOf<USFXGUIMovieLegacyAdapter>([this](USFXGUIMovieLegacyAdapter* p) {
            if (savedPanelVisibility.find(p) == savedPanelVisibility.end()) {
                savedPanelVisibility.emplace(p, true);
            }
            p->SetMovieVisibility(false);
        });
#else
        forEachOf<UBioSFPanel>([this](UBioSFPanel* p) {
            if (savedPanelVisibility.find(p) == savedPanelVisibility.end()) {
                savedPanelVisibility.emplace(p, p->IsVisible != 0);
            }
            p->SetMovieVisibility(false);
        });
#endif
        forEachOf<USFXGameModeBase>([this](USFXGameModeBase* m) {
            if (savedModeVisibility.find(m) == savedModeVisibility.end()) {
#ifdef SDK_TARGET_LE1
                // no bShowSubtitle or bAllowMessageUI on LE1, the subtitles and
                // message box passes below cover those
                savedModeVisibility.emplace(m, HudModeFlags{m->bShowHUD != 0, m->bShowSelection != 0, m->bShowDamageIndicators != 0, m->bShowRadar != 0,
                                                            m->bShowReticles != 0, false, false});
#else
                savedModeVisibility.emplace(m, HudModeFlags{m->bShowHUD != 0, m->bShowSelection != 0, m->bShowDamageIndicators != 0, m->bShowRadar != 0,
                                                            m->bShowReticles != 0, m->bShowSubtitle != 0, m->bAllowMessageUI != 0});
                m->bShowSubtitle = 0;
                m->bAllowMessageUI = 0;
#endif
            }
            m->bShowHUD = 0;
            m->bShowSelection = 0;
            m->bShowDamageIndicators = 0;
            m->bShowRadar = 0;
            m->bShowReticles = 0;
        });
        forEachOf<AHUD>([this](AHUD* h) {
            if (savedHudVisibility.find(h) == savedHudVisibility.end()) {
                savedHudVisibility.emplace(h, (unsigned char)((h->bShowHUD ? 1 : 0) | (h->bShowGameHUD ? 2 : 0)));
            }
            h->bShowHUD = 0;
            h->bShowGameHUD = 0;
#ifdef SDK_TARGET_LE1
            // the message box and actor info overlay are drawn from these flags,
            // UBioMessageBox itself is a canvas box with no visibility API
            ABioHUD* bhud = static_cast<ABioHUD*>(h);
            if (bhud && savedHudExtraFlags.find(bhud) == savedHudExtraFlags.end()) {
                savedHudExtraFlags.emplace(
                    bhud, (unsigned char)((bhud->m_bDisplayActorInfo ? 1 : 0) | (bhud->m_bDisplayDialogData ? 2 : 0) | (bhud->m_bShowHUDThisFrame ? 4 : 0)));
            }
            if (bhud) {
                bhud->m_bDisplayActorInfo = 0;
                bhud->m_bDisplayDialogData = 0;
                bhud->m_bShowHUDThisFrame = 0;
            }
#endif
        });
#ifndef SDK_TARGET_LE1
        forEachOf<ASFXPointOfInterest>([this](ASFXPointOfInterest* poi) {
            if (savedPoiHidden.find(poi) == savedPoiHidden.end()) {
                savedPoiHidden.emplace(poi, poi->bHidden != 0);
            }
            poi->SetHidden(true);
            UActorComponent** data = poi->Components.GetData();
            const int n = (int)poi->Components.Count();
            for (int i = 0; i < n; ++i) {
                UActorComponent* comp = data[i];
                if (!comp || !comp->IsA(UPrimitiveComponent::StaticClass())) {
                    continue;
                }
                UPrimitiveComponent* prim = static_cast<UPrimitiveComponent*>(comp);
                auto found = savedPoiCompHidden.find(prim);
                if (found == savedPoiCompHidden.end()) {
                    savedPoiCompHidden.emplace(prim, prim->HiddenGame != 0);
                }
                prim->HiddenGame = 1;
                prim->SetHidden(true);
            }
        });
        forEachOf<ULensFlareComponent>([this](ULensFlareComponent* f) {
            if (savedFlareActive.find(f) == savedFlareActive.end()) {
                savedFlareActive.emplace(f, f->bIsActive != 0);
            }
            f->SetIsActive(false);
            f->SetHidden(true);
        });
        forEachOf<USFXSelectionModule>([this](USFXSelectionModule* m) {
            if (savedSelectionTargetable.find(m) == savedSelectionTargetable.end()) {
                savedSelectionTargetable.emplace(m, (unsigned char)((m->m_bTargetable ? 1 : 0) | (m->m_bCombatTargetable ? 2 : 0)));
            }
#ifdef SDK_TARGET_LE3
            m->SetTargetable(false, false);
            m->SetCombatTargetable(false, false);
#else
            m->SetTargetable(false);
            m->SetCombatTargetable(false);
#endif
        });
#endif
        forEachOf<UBioPlayerSelection>([this](UBioPlayerSelection* s) {
            s->m_oCurrentSelectionTarget = nullptr;
#ifdef SDK_TARGET_LE3
            // m_oPendingSelectionTarget removed in LE3
#elif !defined(SDK_TARGET_LE1)
            // neither m_oPendingSelectionTarget nor SelectionFlareComp on LE1
            s->m_oPendingSelectionTarget = nullptr;
#endif
            s->m_oLastSelectionTarget = nullptr;
#if !defined(SDK_TARGET_LE1)
            if (s->SelectionFlareComp) {
                s->SelectionFlareComp->SetIsActive(false);
                s->SelectionFlareComp->SetHidden(true);
            }
#endif
#ifdef SDK_TARGET_LE1
            // no USFXSelectionModule on LE1, so there is no unselectable switch. the
            // reticle comes from m_lSelectionsInRange, empty it instead
            if (s->m_lSelectionsInRange.Count() > 0) {
                if (savedSelectionsInRange.find(s) == savedSelectionsInRange.end()) {
                    std::vector<FBioSelection> kept;
                    for (int i = 0; i < (int)s->m_lSelectionsInRange.Count(); ++i) {
                        kept.push_back(s->m_lSelectionsInRange.GetData()[i]);
                    }
                    savedSelectionsInRange.emplace(s, kept);
                }
                s->m_lSelectionsInRange.Clear();
            }
#endif
        });
#ifdef SDK_TARGET_LE1
        // subtitles live on ABioWorldInfo::m_Subtitles here, not the game mode.
        // ClearHistory() is the API, leave m_sSubtitle alone: the SDK's FString owns
        // its buffer and writing it would free the game's
        forEachOf<ABioWorldInfo>([](ABioWorldInfo* wi) {
            if (UBioSubtitles* subs = wi ? wi->GetSubtitles() : nullptr) {
                subs->ClearHistory();
            }
        });
#endif
        forEachOf<UPrimitiveComponent>([this](UPrimitiveComponent* prim) {
            std::string compName = FStringToString(prim->GetName());
            std::string compClass = FStringToString(prim->Class->GetName());
            bool shouldHide = (
                // compName.find("Flare") != std::string::npos || compName.find("Selection") != std::string::npos ||
                compName.find("Bracket") != std::string::npos || compName.find("Indicator") != std::string::npos ||
                compName.find("Marker") != std::string::npos ||
                compName.find("Reticle") != std::string::npos
#ifdef SDK_TARGET_LE1
                // no ASFXPointOfInterest and no BioLoot package on LE1, so loot and
                // interaction markers are matched by name instead
                || compName.find("Loot") != std::string::npos || compName.find("Interact") != std::string::npos ||
                compName.find("Pickup") != std::string::npos || compName.find("Container") != std::string::npos || compName.find("Supply") != std::string::npos
#endif
                // compClass.find("Particle") != std::string::npos || compClass.find("Sprite") != std::string::npos
            );
            if (shouldHide) {
                if (savedPoiCompHidden.find(prim) == savedPoiCompHidden.end()) {
                    savedPoiCompHidden.emplace(prim, prim->HiddenGame != 0);
                }
                prim->HiddenGame = 1;
                prim->SetHidden(true);
            }
        });
    } else {
        for (auto it = savedPanelVisibility.begin(); it != savedPanelVisibility.end();) {
            if (isLiveObject(it->first)) {
                it->first->SetMovieVisibility(it->second ? true : false);
            }
            it = savedPanelVisibility.erase(it);
        }
        for (auto it = savedModeVisibility.begin(); it != savedModeVisibility.end();) {
            if (isLiveObject(it->first)) {
                it->first->bShowHUD = it->second.hud;
                it->first->bShowSelection = it->second.selection;
                it->first->bShowDamageIndicators = it->second.damage;
                it->first->bShowRadar = it->second.radar;
                it->first->bShowReticles = it->second.reticles;
#ifndef SDK_TARGET_LE1
                it->first->bShowSubtitle = it->second.subtitles;
                it->first->bAllowMessageUI = it->second.messages;
#endif
            }
            it = savedModeVisibility.erase(it);
        }
        for (auto it = savedHudVisibility.begin(); it != savedHudVisibility.end();) {
            if (isLiveObject(it->first)) {
                it->first->bShowHUD = (it->second & 1) != 0;
                it->first->bShowGameHUD = (it->second & 2) != 0;
            }
            it = savedHudVisibility.erase(it);
        }
#ifdef SDK_TARGET_LE1
        for (auto it = savedHudExtraFlags.begin(); it != savedHudExtraFlags.end();) {
            if (isLiveObject(it->first)) {
                it->first->m_bDisplayActorInfo = (it->second & 1) != 0;
                it->first->m_bDisplayDialogData = (it->second & 2) != 0;
                it->first->m_bShowHUDThisFrame = (it->second & 4) != 0;
            }
            it = savedHudExtraFlags.erase(it);
        }
        for (auto it = savedSelectionsInRange.begin(); it != savedSelectionsInRange.end();) {
            if (isLiveObject(it->first)) {
                it->first->m_lSelectionsInRange.Clear();
                for (const FBioSelection& sel : it->second) {
                    it->first->m_lSelectionsInRange.Add(sel);
                }
            }
            it = savedSelectionsInRange.erase(it);
        }
#else
        for (auto it = savedPoiHidden.begin(); it != savedPoiHidden.end();) {
            if (isLiveObject(it->first)) {
                it->first->SetHidden(it->second);
            }
            it = savedPoiHidden.erase(it);
        }
#endif
        for (auto it = savedPoiCompHidden.begin(); it != savedPoiCompHidden.end();) {
            if (isLiveObject(it->first)) {
                it->first->HiddenGame = it->second;
                it->first->SetHidden(!it->second);
            }
            it = savedPoiCompHidden.erase(it);
        }
#ifndef SDK_TARGET_LE1
        for (auto it = savedFlareActive.begin(); it != savedFlareActive.end();) {
            if (isLiveObject(it->first)) {
                it->first->SetIsActive(it->second);
                it->first->SetHidden(!it->second);
            }
            it = savedFlareActive.erase(it);
        }
        for (auto it = savedSelectionTargetable.begin(); it != savedSelectionTargetable.end();) {
            if (isLiveObject(it->first)) {
#ifdef SDK_TARGET_LE3
                it->first->SetTargetable((it->second & 1) != 0, false);
                it->first->SetCombatTargetable((it->second & 2) != 0, false);
#else
                it->first->SetTargetable((it->second & 1) != 0);
                it->first->SetCombatTargetable((it->second & 2) != 0);
#endif
            }
            it = savedSelectionTargetable.erase(it);
        }
#endif
    }
}

void Engine::setGameUIHidden(bool hidden) {
    isGameUIHiddenState = hidden;
    postGameThreadTask([this, hidden]() {
        if (isGameUIHiddenState != hidden) {
            return;
        }
        applyHUDVisibility();
    });
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
