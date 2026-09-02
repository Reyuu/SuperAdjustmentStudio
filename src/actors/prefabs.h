#ifndef SAS_PREFABS_H
#define SAS_PREFABS_H

#include "util.h"
#include <LESDK/Includes.LE2.hpp>
#include <mutex>
#include <string>
#include <vector>

// ME2 -> no public runtime for instancing UPrefab assets
struct PrefabTemplate {
        std::string fullName;   // object path name (unique)
        std::string name;       // prefab object name
        std::string package;    // package owning package
        std::string nameLower;  // lowercase fullName
        int archetypeCount = 0; // number of actor archetypes it contains
};

// one instanced prefab (all spawned archetype actors)
struct PrefabEntry {
        std::string name;            // first spawned actor's name
        std::string prefabName;      // source prefab full name
        std::vector<AActor*> actors; // spawned actors (archetype copies)
        std::vector<AActor*> lights; // point lights parented to this prefab
};

enum class LightConfig {
    Corner = 0,
    Face = 1,
    CornerFace = 2
};

struct PrefabManager {
    public:
        void renderUI();
        void markNeedsRefresh() {
            needsRefresh = true;
        }
        void collectPrefabs();
        void spawnPrefab(const std::string& fullName);
        void removePrefab(const PrefabEntry& entry);
        void removePrefabByName(const std::string& name, const std::string& prefabName);
        void updateActivePrefabs();
        void spawnCornerLights();
        void spawnFaceLights();
        void spawnLightsByConfig(LightConfig cfg);
        void updateLightExpansion(float offset);
        void forceUpdateMaterial();
        void onActorMoved(AActor* actor);
        void nudgeLights(int idx);

    private:
        int findEntryIndexByName(const std::string& name, const std::string& prefabName) const;
        void fixupSelectedActive();
        void spawnPrefabLight(const Transform& t, int entryIdx);
        void renderLightsPanel(const std::vector<PrefabEntry>& entries, int selActive);
        void renderLightBulkControls(const std::vector<PrefabEntry>& entries, int selActive);
        void renderLightExpansionControl();
        void renderLightRow(AActor* light, size_t lightIndex);
        void renderAvailablePrefabsPanel();
        void renderActivePrefabsPanel(const std::vector<PrefabEntry>& entries, int& selActive);
        void renderMaterialControls();
        void renderLightSpawnControls();
        mutable std::recursive_mutex mutex;
        std::vector<PrefabTemplate> available;
        std::vector<PrefabEntry> prefabEntries;
        std::string selectedPrefabName;
        int selectedActive = -1;
        LightConfig selectedLightConfig = LightConfig::Corner;
        char searchFilter[256] = "";
        float lastAutoUpdate = 0.0f;
        float lastCollect = 0.0f;
        float lastExpansionUpdate = 0.0f;
        float lastMaterialUpdate = 0.0f;
        AActor* pendingMaterialActor = nullptr;
        bool needsRefresh = false;
        float lightExpansion = 95.0f;
        float lightBrightness = 0.4f;
        float lightRadius = 350.0f;
        float lightColor[3] = {1.0f, 1.0f, 1.0f};
};
#endif // SAS_PREFABS_H
