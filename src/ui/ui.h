#ifndef SAS_UI_H
#define SAS_UI_H

#include <atomic>
#include <string>
#include <vector>

#include <windows.h>

#include "bones.h"
#include "game_window.h"
#include "helpers/toast_notifications.h"
#include "native_renderer.h"
#include "pcc_parser.h"
#include "util.h"

struct ClassEntry {
    public:
        std::string package;
        std::string name;
        std::string fullName;
};

struct PackageEntry {
        std::string path;      // full path (for load)
        std::string relPath;   // path relative to BIOGame (for display)
        std::string name;      // filename (for display)
        std::string nameLower; // lowercase (for filtering)
};

class UI {
    public:
        std::atomic<bool>& showUI() {
            return showUIstate;
        }
        std::vector<std::string>& pawnNames() {
            return pawnNamesVector;
        }
        int& pawnIndex() {
            return pawnIndexInt;
        }
        std::vector<std::string>& pinnedNames() {
            return pinnedNamesVector;
        }

        std::string getSelectedPawnName() {
            if (pawnIndexInt < 0 || pawnIndexInt >= (int)pawnNamesVector.size()) {
                return "";
            }
            return pawnNamesVector[pawnIndexInt];
        }

        const Transform& getSpawnTransform() const {
            return spawnTransform;
        }
        const Transform& getSelectedTransform() const {
            return selectedTransform;
        }

        void collectPawns();
        void collectClasses();
        void collectAnimations(const std::string& pawnName);
        void collectPackages();

        void selectActor(AActor* actor);
        bool renderTransformEditor(Transform& t, const char* idPrefix);
        void applyUIInputState(GameWindow& window);
        void renderOverlayContents(NativeRenderer& renderer);

        ToastNotificationManager toastManager;

    private:
        void renderControlsSection();
        void renderSelectionSection();
        void renderSpawnSection();
        void renderSelectionTarget();
        void renderSelectionTransform();
        void renderSelectionAnimation();
        void renderSelectionBones();
        void renderBonesDirectBones(const std::string& pawn);
        void renderBonesReset();
        void renderSelectionOtherProps();
        void renderSpawnClassList();
        void renderSpawnTransform();
        void renderSpawnOtherProps();
        void renderPackagesSection();
        void renderPackagePreview();
        bool renderPackagePreviewNode(int index, const std::string& filter);

        std::atomic<bool> showUIstate{false};
        std::vector<std::string> pawnNamesVector;
        int pawnIndexInt = 0;
        std::vector<std::string> pinnedNamesVector;

        bool pauseTime = false;
        bool advancedSelection = false;
        bool animationIncludeAll = false;
        std::vector<std::string> animationNames;
        int animationIndex = 0;
        std::string animationPawn;
        float lastPawnRefresh = 0.0f;

        std::vector<ClassEntry> classes;
        int classIndex = 0;
        std::string selectedClassFullName;
        char classSearch[128] = "";
        float lastClassRefresh = 0.0f;

        std::vector<PackageEntry> packages;
        int packageIndex = 0;
        char packageSearch[128] = "";
        float lastPackageRefresh = 0.0f;

        bool packagePreviewOpen = false;
        PCCFile packagePreview;
        std::string packagePreviewName;
        char packagePreviewSearch[128] = "";

        Transform selectedTransform;
        Transform spawnTransform;

        bool transformToApply = false;
        float transformLastEdit = 0.0f;
        float transformLastApply = 0.0f;
        std::string transformPawn;

        char animationSearch[128] = "";

        bool pendingCollectPawns = false;

        bool floatEnabled = false;
        std::string floatPawn;

        // Bones crap
        int meshTargetIndex = 0; // MeshTarget: 0 = Body, 1 = Head //TODO: make it an enum

        std::string bonePawn;
        std::vector<BonePoseInfo> bones;
        int boneIndex = 0;
        char boneSearch[128] = "";
        BonePoseInfo boneEdit;
        bool boneToApply = false;
        float boneLastEdit = 0.0f;
        float boneLastApply = 0.0f;

        bool boneListTried = false;

        void refreshBoneList(const std::string& pawnName);
};

#endif // SAS_UI_H
