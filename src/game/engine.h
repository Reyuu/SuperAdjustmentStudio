#ifndef SAS_ENGINE_H
#define SAS_ENGINE_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "hook_manager.h"
#include "sdk.h"
#include "util.h"
#include <LESDK/Includes.hpp>

struct PackageLoadTask {
    public:
        std::string package;
        std::function<void()> onLoaded;
};

struct HudModeFlags {
        bool hud;
        bool selection;
        bool damage;
        bool radar;
        bool reticles;
        bool subtitles;
        bool messages;
};

class Engine {

    public:
        bool& isGameUIHidden() {
            return isGameUIHiddenState;
        }
        std::atomic<bool>& isCameraDragActive() {
            return isCameraDragActiveState;
        }
        // spawned names are written on the game thread (spawn / delete) and read on the
        // render thread (selection lists), so this hands out copies and does the mutation
        // behind a lock rather than leaking the vector
        // the exact instances we spawned. LE1's SpawnPawn can reuse a name already in
        // the level, so the name list cannot identify an actor on its own
        std::vector<AActor*> Engine::spawnedActors() const {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            return spawnedActorsVector;
        }
        bool Engine::isSpawnedActor(AActor* actor) const {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            return std::find(spawnedActorsVector.begin(), spawnedActorsVector.end(), actor) != spawnedActorsVector.end();
        }
        void Engine::addSpawnedActor(AActor* actor) {
            if (!actor) {
                return;
            }
            // inline, addSpawnedName takes the same non-recursive mutex and would
            // deadlock here
            const std::string nm = FStringToUtf8(actor->GetName());
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            if (std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), nm) == spawnedNamesVector.end()) {
                spawnedNamesVector.push_back(nm);
            }
            if (std::find(spawnedActorsVector.begin(), spawnedActorsVector.end(), actor) == spawnedActorsVector.end()) {
                spawnedActorsVector.push_back(actor);
            }
        }
        void Engine::removeSpawnedActor(AActor* actor) {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            spawnedActorsVector.erase(std::remove(spawnedActorsVector.begin(), spawnedActorsVector.end(), actor), spawnedActorsVector.end());
        }
        std::vector<std::string> Engine::spawnedNames() const {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            return spawnedNamesVector;
        }
        bool Engine::isSpawnedName(const std::string& name) const {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            return std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), name) != spawnedNamesVector.end();
        }
        void Engine::addSpawnedName(const std::string& name) {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            if (std::find(spawnedNamesVector.begin(), spawnedNamesVector.end(), name) == spawnedNamesVector.end()) {
                spawnedNamesVector.push_back(name);
            }
        }
        void Engine::removeSpawnedName(const std::string& name) {
            std::lock_guard<std::mutex> lock(spawnedNamesMtx);
            spawnedNamesVector.erase(std::remove(spawnedNamesVector.begin(), spawnedNamesVector.end(), name), spawnedNamesVector.end());
        }

        void postGameThreadTask(std::function<void()> fn);
        void drainGameThreadTasks();

        void consoleCommand(const std::string& command);

        void postPackageLoad(const std::string& package, std::function<void()> onLoaded);
        void drainPackageLoads();

        void setPause(bool pause);

        AActor* findActorByName(const std::string& name);
        // exact match, for telling SFXPawn_Jack_1 apart from SFXPawn_Jack_10
        AActor* findActorByExactName(const std::string& name);
        AActor* playerPawn();
        USkeletalMeshComponent* findPawnMesh(const std::string& pawnName);

        AActor* spawnClass(const std::string& className, const Transform& t);
        std::string diagnoseClass(const std::string& className);
        void removeActor(const std::string& name);
        void loadTransformFromPawn(const std::string& pawnName, Transform& t);
        void loadTransformFromActor(AActor* actor, Transform& t);
        void setTransform(const std::string& targetName, const Transform& t);
        void setTransform(AActor* actor, const Transform& t);
        void setFloat(const std::string& targetName, bool enable);

        void applyHUDVisibility();
        void setGameUIHidden(bool hidden);
        void freezeLook(bool freeze);

        void initTickHook(HookManager& hookManager, SDKContext& sdk);

    private:
        std::mutex gameTasksMutex;
        std::vector<std::function<void()>> gameTasks;

        std::mutex loadTasksMutex;
        std::vector<PackageLoadTask> loadTasks;

        bool isGameUIHiddenState = false;
        std::atomic<bool> isCameraDragActiveState{false};
        mutable std::mutex spawnedNamesMtx;
        std::vector<std::string> spawnedNamesVector;
        std::vector<AActor*> spawnedActorsVector;

        // engine-level look freeze
        unsigned char savedIgnoreLook = 0;
        unsigned char savedIgnoreMove = 0;
        bool lookFrozen = false;

        // ignore collisions
        unsigned char floatOldPhysics = 0;
        bool floatOldCollideActors = false;
        bool floatOldCollideWorld = false;
        bool floatOldBlockActors = false;
        bool floatOldBlockRigidBody = false;

#ifdef SDK_TARGET_LE3
        std::unordered_map<USFXGUIMovieLegacyAdapter*, bool> savedPanelVisibility;
#else
        std::unordered_map<UBioSFPanel*, bool> savedPanelVisibility;
#endif

        std::unordered_map<USFXGameModeBase*, HudModeFlags> savedModeVisibility;
        std::unordered_map<AHUD*, unsigned char> savedHudVisibility;
        std::unordered_map<UPrimitiveComponent*, bool> savedPoiCompHidden;
#ifndef SDK_TARGET_LE1
        std::unordered_map<ASFXPointOfInterest*, bool> savedPoiHidden;
        std::unordered_map<ULensFlareComponent*, bool> savedFlareActive;
        std::unordered_map<USFXSelectionModule*, unsigned char> savedSelectionTargetable;
#endif
#ifdef SDK_TARGET_LE1
        // LE1 hides the overlay and reticle through different objects than LE2's
        // USFXGameModeBase flags. the subtitles object refills itself as the
        // conversation runs and UBioMessageBox has no visibility API at all, so
        // both are driven through the ABioHUD flags and ClearHistory
        std::unordered_map<ABioHUD*, unsigned char> savedHudExtraFlags;
        std::unordered_map<UBioPlayerSelection*, std::vector<FBioSelection>> savedSelectionsInRange;
#endif
};

#endif // SAS_ENGINE_H