#ifndef SAS_NPCS_H
#define SAS_NPCS_H

#include "util.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <functional>
#include <iosfwd>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifndef SDK_TARGET_LE1

#include <LESDK/Includes.hpp>

enum PawnMeshSlot {
    PAWN_SLOT_BODY = 0,
    PAWN_SLOT_HEAD,
    PAWN_SLOT_HAIR,
    PAWN_SLOT_HEADGEAR,
    PAWN_SLOT_VISOR,
    PAWN_SLOT_FACEPLATE,
    PAWN_SLOT_COUNT
};

enum NPCPicker {
    NPC_PICK_SPAWN_BODY = 0,
    NPC_PICK_SPAWN_HEAD,
    NPC_PICK_SPAWN_MORPH,
    NPC_PICK_SPAWN_HAIR,
    NPC_PICK_SPAWN_HEADGEAR,
    NPC_PICK_SPAWN_VISOR,
    NPC_PICK_SPAWN_FACEPLATE,
    NPC_PICK_SPAWN_CRUST,
    NPC_PICK_LIVE_BODY,
    NPC_PICK_LIVE_HEAD,
    NPC_PICK_LIVE_MORPH,
    NPC_PICK_LIVE_HAIR,
    NPC_PICK_LIVE_HEADGEAR,
    NPC_PICK_LIVE_VISOR,
    NPC_PICK_LIVE_FACEPLATE,
    NPC_PICK_LIVE_CRUST,
    NPC_PICKER_COUNT
};

struct PawnSpawnConfig {
        std::string classFullName; // "Class SFXGame.BioPawn"
        std::string assets[PAWN_SLOT_COUNT];
        std::string morphHead; // UBioMorphFace path
        std::string lifeCrust; // UBioVFXTemplate path (LE2 only)
        float lifeCrustLifeTime = 9999.0f;
};

// a pawn class found in a .pcc, with the package that owns it
struct ScannedPawn {
        std::string classFullName;
        std::string packagePath;
        std::string packageName;
        std::string displayName;
};

// one index row, built in or generated into %APPDATA%
struct PawnIndexRow {
        std::string displayName;
        std::string exportName;
        std::string packageName;   // runtime package, used to resolve the class
        std::string sourcePackage; // the .pcc to open
        std::string category;
};

// authored display names, keyed by export name
struct PawnPreset {
        const char* displayName;
        const char* classShortName;
        const char* category;
};

struct PawnSlotRow {
        int picker; // NPCPicker id
        int slot;   // PawnMeshSlot, or -1 for the lists that are not mesh slots
        const char* labelKey;
};

class NPCManager {
    public:
        void renderSpawnUI();
        void renderSelectionUI();

        void collectAssets(bool force);
        void applyAssets();

        void spawnNPC(const PawnSpawnConfig& config, const Transform& spawnTransform);

        void applySlotToPawn(const std::string& pawnName, int slot, const std::string& assetName);
        void withPawn(const std::string& pawnName, const std::function<void(ABioPawn*)>& fn);
        std::vector<std::string> currentAddonPicks() const;
        std::string findPackagePath(const std::string& sourcePackage);
        void applyMorphHeadToPawn(const std::string& pawnName, const std::string& morphName);
        void reapplyMorphHead(const std::string& pawnName);
        void applyMorphColor(const std::string& pawnName, int colorIndex, const FLinearColor& color);
        void refreshMorphBuffers(UBioMorphFace* morph);
#ifndef SDK_TARGET_LE3
        void applyLifeCrustToPawn(const std::string& pawnName, const std::string& templateName, float lifeTime);
#endif
        void deleteNPC(const std::string& pawnName);

        std::string dumpPawn(ABioPawn* pawn);

        static USkeletalMeshComponent* slotComponent(ABioPawn* pawn, int slot);
        static const char* slotLabelKey(int slot);

    private:
        void applySpawnConfig(ABioPawn* pawn, const PawnSpawnConfig& config);
        bool applySlot(ABioPawn* pawn, int slot, USkeletalMesh* mesh);
        static ABioPawn* findAppearanceTemplate(UClass* cls);
        void copyAppearanceFrom(ABioPawn* pawn, ABioPawn* source);
        void applyMorphHead(ABioPawn* pawn, UBioMorphFace* morph);
        // LE2 only, the others have no MergeMeshes
        bool applyCompositeAddons(ABioPawn* pawn, const std::vector<USkeletalMesh*>& addons);
        bool rebuildHeadComposite(ABioPawn* pawn, const std::vector<std::string>& addonPaths);
        static bool isAddonSlot(int slot);
        static const PawnSlotRow slotRows[];
        static const int slotRowCount;
        int addonPickerForSlot(int slot) const;
        USkeletalMesh* plainHeadMeshFor(ABioPawn* pawn) const;
        void rememberBaseHead(ABioPawn* pawn, USkeletalMesh* mesh);
#ifndef SDK_TARGET_LE3
        void applyLifeCrust(ABioPawn* pawn, const std::string& templateName, float lifeTime);
        void dumpLifeCrust(std::ostream& ss, ABioPawn* pawn);
#endif
        void dumpComponent(std::ostream& ss, const char* label, USkeletalMeshComponent* mesh);
        void dumpMorphHead(std::ostream& ss, UBioMorphFace* morph);

        // slot < 0 for lists that are not mesh slots
        void renderAssetPicker(int picker, int slot, const std::vector<std::string>& items, const std::vector<std::string>& itemsLower,
                               const std::function<void(const std::string&)>& onPicked, const std::string& filter);
        void renderPickerFilter(const char* id, const char* hint, char* filter);
        void renderPickerTable(const std::vector<PawnSlotRow>& rows, const char* filterId, const char* filterHint, char* filter,
                               const std::function<void(const PawnSlotRow&, const std::string&)>& row);
        const std::vector<PawnSlotRow>& meshPickerRows(int pickerBase) const;
        const std::vector<PawnSlotRow>& facePickerRows(int morphPicker, int crustPicker) const;
        void rebuildResidentPawns();
        int indexVersion = 0; // bumped whenever indexRows changes
        void renderPawnClassList();
        void renderPawnPresets();
        void renderPawnIndexList();
        void renderLiveCustomization(ABioPawn* pawn, const std::string& pawnName);
        void renderSpawnedNpcList();
        void spawnFromIndexRow(const PawnIndexRow& row);
        void ensureIndexLoaded();
        void generateUserIndex();
        void pollUserIndex();
        static std::filesystem::path userIndexPath();
        static UClass* resolvePawnClass(const std::string& packageName, const std::string& exportName);
        bool isPawnExportResident(const std::string& exportName) const;
        static std::string inferDisplayName(const std::string& classFullName);
        void ensureMorphResident(const std::string& morphPath, const std::function<void()>& onLoaded);
        void renderMorphFacePanel(ABioPawn* pawn, const std::string& pawnName);
        void renderMaterialsPanel(ABioPawn* pawn);
        void setMaterialOnPawn(const std::string& pawnName, int slot, int elementIndex, const std::string& materialPath);
        void resetMaterialOnPawn(const std::string& pawnName, int slot, int elementIndex);
        void recreateMaterialInstance(const std::string& pawnName, int slot, int elementIndex);
        void reapplyMaterialOverrides(ABioPawn* pawn);
        void reapplyHeadMesh(ABioPawn* pawn);
        void applyMorphToPawn(ABioPawn* pawn);
        void logMorphBufferProbe(ABioPawn* pawn, const char* when);
        void handlePendingSelect();
        int referenceBonesForPicker(int picker) const;
        ABioPawn* currentPawn() const;
        bool isKnownGoodForSlot(int slot, const std::string& pathLower) const;
        void collectKnownGoodMeshes();

        std::vector<std::string> pawnClasses; // "Class SFXGame.BioPawn"
        std::vector<std::string> pawnClassesLower;
        std::vector<std::string> residentPawnShortNamesLower;
        std::vector<std::string> skeletalMeshes; // USkeletalMesh paths
        std::vector<std::string> skeletalMeshesLower;
        std::vector<int> skeletalMeshBones;  // bone count per entry, for the picker
        std::vector<std::string> morphFaces; // UBioMorphFace paths
        std::vector<std::string> morphFacesLower;
        std::vector<std::string> materialAssets; // UMaterialInterface paths
        std::vector<std::string> materialAssetsLower;
#ifndef SDK_TARGET_LE3
        std::vector<std::string> crustTemplates; // UBioVFXTemplate paths
        std::vector<std::string> crustTemplatesLower;
#endif
        std::vector<ScannedPawn> loadedPawnPresets; // resident classes, for the preset list
        // lower case, harvested from the pawns in the level
        std::vector<std::string> validMeshPaths[PAWN_SLOT_COUNT];
        bool showAllAssets = false;
        int selectedIndexEntry = -1;
        std::vector<PawnIndexRow> indexRows;
        bool indexRowsLoaded = false;
        std::future<void> indexFuture;
        std::atomic<bool> indexScanRunning{false};
        std::atomic<bool> indexScanCancel{false};
        std::atomic<int> indexScanDone{0};
        std::atomic<int> indexScanTotal{0};
        std::atomic<int> indexScanFound{0};
        bool indexIncludeMods = true;
        std::string selectedResidentClass;
        mutable std::mutex packageCacheMtx;
        std::map<std::string, std::string> packageCache; // lower pcc stem -> path
        bool packageCacheValid = false;
        // "<pawn>|<slot>|<element>" -> material path
        std::map<std::string, std::string> materialOverrides;

        // ImGui writes buf_size bytes into whatever it is handed, so these cannot
        // be std::string: data() carries no capacity guarantee
        static constexpr int FILTER_SIZE = 128;
        char meshPickerFilter[FILTER_SIZE] = {};
        char facePickerFilter[FILTER_SIZE] = {};
        char classListFilter[FILTER_SIZE] = {};
        char characterSearch[FILTER_SIZE] = {};
        char materialPickerFilter[FILTER_SIZE] = {};
        // revalidated on a timer, not per frame
        mutable std::string cachedPawnName;
        mutable ABioPawn* cachedPawn = nullptr;
        mutable double cachedPawnTime = -1.0;
        std::vector<std::string> spawnedNpcNames;
        double spawnedNpcRefreshTime = -1.0;
        std::vector<std::string> knownSpawnable; // pawn classes that spawned OK this session

        std::string picks[NPC_PICKER_COUNT];
        std::string selectedPawnClass;
#ifndef SDK_TARGET_LE3
        float spawnCrustLifeTime = 9999.0f;
#endif
        bool spawnEmptyPawn = false;
        // MergeMeshes is additive, so composes start from the plain head mesh
        std::map<std::string, USkeletalMesh*> baseHeadByPawn;
        std::vector<float> morphColorValues; // rgba per color override

        std::mutex pendingSelectMtx;
        std::string pendingSelect;
        mutable std::mutex stateMtx; // guards the fields below, written on the game thread
        bool assetsCollected = false;
        bool assetsToApply = true;
};

// resolved by object path on the game thread, right before use
USkeletalMesh* findSkeletalMeshByPath(const std::string& path);
UBioMorphFace* findMorphHeadByPath(const std::string& path);
#ifndef SDK_TARGET_LE3
UBioVFXTemplate* findVFXTemplateByPath(const std::string& path);
#endif

#endif // SDK_TARGET_LE1

#endif // SAS_NPCS_H