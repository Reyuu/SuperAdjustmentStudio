#ifndef SAS_SNAPSHOTS_H
#define SAS_SNAPSHOTS_H

#include <filesystem>
#include <string>
#include <vector>
#include "json.hpp"
#include "IconsFontAwesome6.h"

using json = nlohmann::json;

enum SnapshotSection {
    SnapshotCamera = 0,
    SnapshotFovRoll,
    SnapshotFilter,
    SnapshotLut,
    SnapshotAnimation,
    SnapshotVfx,
    SnapshotBones,
    SnapshotSelection,
    SnapshotLighting,
    SnapshotPrefabs,
    SnapshotParticles,
    SnapshotCameraPP,
    SnapshotSectionCount
};

struct SnapshotSectionInfo {
    public:
        SnapshotSection section;
        const char* key;
        const char* icon;
        const char* labelKey;
};

static const SnapshotSectionInfo SNAPSHOT_SECTION_INFOS[] = {
    {SnapshotCamera,    "camera",    ICON_FA_VIDEO,            "ui.snapshots.section_camera"   },
    {SnapshotFovRoll,   "fovRoll",   ICON_FA_MAGNIFYING_GLASS, "ui.snapshots.section_fov_roll" },
    {SnapshotFilter,    "filter",    ICON_FA_WAND_SPARKLES,    "ui.snapshots.section_filter"   },
    {SnapshotLut,       "lut",       ICON_FA_PALETTE,          "ui.snapshots.section_lut"      },
    {SnapshotAnimation, "animation", ICON_FA_PERSON_RUNNING,   "ui.snapshots.section_animation"},
    {SnapshotVfx,       "vfx",       ICON_FA_BURST,            "ui.snapshots.section_vfx"      },
    {SnapshotBones,     "bones",     ICON_FA_BONE,             "ui.snapshots.section_bones"    },
    {SnapshotSelection, "selection", ICON_FA_CROSSHAIRS,       "ui.snapshots.section_selection"},
    {SnapshotLighting,  "lighting",  ICON_FA_LIGHTBULB,        "ui.snapshots.section_lighting" },
    {SnapshotPrefabs,   "prefabs",   ICON_FA_BOX,              "ui.snapshots.section_prefabs"  },
    {SnapshotParticles, "particles", ICON_FA_STAR,             "ui.snapshots.section_particles"},
    {SnapshotCameraPP,  "cameraPP",  ICON_FA_SLIDERS,          "ui.snapshots.section_camera_pp"},
};

struct SnapshotFileEntry {
    public:
        std::string filename;
        std::string name;
        std::vector<std::string> sections;
        std::string icons;
};

class SnapshotsManager {
    public:
        void saveSnapshot();
        void loadSnapshot(std::string filename);
        void renderUi();
        void renderWizards();
        void renderLoadWizard();
        void renderSaveWizard();

    private:
        json getCameraData() const;
        void applyCameraData(const json& cameraData);

        json getFovRollData() const;
        void applyFovRollData(const json& fovRollData);

        json getCameraPostProcessData() const;
        void applyCameraPostProcessData(const json& cameraPostProcessData);

        json getFilterData() const;
        void applyFilterData(const json& filterData);

        json getLutData() const;
        void applyLutData(const json& lutData);

        json getAnimationData() const;
        void applyAnimationData(const json& animationData);

        json getVfxData() const;
        void applyVfxData(const json& vfxData);

        json getBonesData() const;
        void applyBonesData(const json& bonesData);

        json getSelectionData() const;
        void applySelectionData(const json& selectionData);

        json getLightingData() const;
        void applyLightingData(const json& lightingData);

        json getPrefabsData() const;
        void applyPrefabsData(const json& prefabsData);

        json getParticlesData() const;
        void applyParticlesData(const json& particlesData);

        static std::filesystem::path snapshotsDir();
        static void migrateLegacyPresetsDir();
        static std::string sanitizeSnapshotName(const std::string& name);
        bool hasSectionData(SnapshotSection section);
        void openSaveWizard();
        void openLoadWizard();
        void refreshSnapshotFiles();
        void applyLoadedSnapshot(const json& snapshot);

        bool showSaveWizard = false;
        bool showLoadWizard = false;
        bool savePopupOpened = false;
        bool loadPopupOpened = false;
        bool saveModalLogged = false;
        bool loadModalLogged = false;
        char saveName[256] = "";
        bool saveSections[SnapshotSectionCount] = {};
        bool saveConfirmOverwrite = false;

        std::vector<SnapshotFileEntry> loadFiles;
        int loadIndex = -1;
        bool loadSections[SnapshotSectionCount] = {};
        std::string lastLoadedFile;
        std::string lastLoadedName;
};

#endif // SAS_SNAPSHOTS_H
