#ifndef SAS_ENGINE_H
#define SAS_ENGINE_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "hook_manager.h"
#include "sdk.h"
#include "util.h"
#include <LESDK/Includes.LE2.hpp>

struct PackageLoadTask {
    public:
        std::string package;
        std::function<void()> onLoaded;
};

class Engine {

    public:
        bool& isGameUIHidden() {
            return isGameUIHiddenState;
        }
        std::atomic<bool>& isCameraDragActive() {
            return isCameraDragActiveState;
        }
        std::vector<std::string>& spawnedNames() {
            return spawnedNamesVector;
        }

        void postGameThreadTask(std::function<void()> fn);
        void drainGameThreadTasks();

        void postPackageLoad(const std::string& package, std::function<void()> onLoaded);
        void drainPackageLoads();

        void setPause(bool pause);

        AActor* findActorByName(const std::string& name);
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
        void freezeLook(bool freeze);

        void initTickHook(HookManager& hookManager, SDKContext& sdk);

    private:
        std::mutex gameTasksMutex;
        std::vector<std::function<void()>> gameTasks;

        std::mutex loadTasksMutex;
        std::vector<PackageLoadTask> loadTasks;

        bool isGameUIHiddenState = false;
        std::atomic<bool> isCameraDragActiveState{false};
        std::vector<std::string> spawnedNamesVector;

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

        // FIXME: hide ui - borked
        unsigned long savedHUDShowBits = 0;
        bool hudShowBitsSaved = false;
};

#endif // SAS_ENGINE_H