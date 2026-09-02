#ifndef SAS_LIGHTS_H
#define SAS_LIGHTS_H

#include <LESDK/Includes.LE2.hpp>
#include <mutex>
#include <string>
#include <vector>

// tried to implement pointlight, but pointlight is a bitch to spawn.

struct LightEntry {
        std::string name;
        std::string type;
        AActor* actor = nullptr;
};

struct LightSettings {
        bool enabled = true;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float brightness = 1.0f;
        float radius = 1024.0f;
        float falloffExponent = 2.0f;

        float innerConeAngle = 0.0f;
        float outerConeAngle = 44.0f;
        float lightShaftConeAngle = 0.0f;

        float bloomScale = 0.0f;
        float bloomThreshold = 0.0f;
        float bloomScreenBlendThreshold = 0.0f;
        float bloomTint[3] = {1.0f, 1.0f, 1.0f};

        float shadowRadiusMultiplier = 1.0f;
        int shadowProjectionTechnique = 0;
        int shadowFilterQuality = 0;
        int lightShadowMode = 0;
        bool castShadows = true;
        bool castDynamicShadows = true;

        bool renderLightShafts = false;
        bool isPoint = false;
        bool isSpot = false;
};

class LightManager {
    public:
        void renderUI();

    private:
        void addLight(AActor* actor, const std::string& type);
        void removeLight(AActor* actor);
        void updateActiveLights();
        void selectLight(AActor* actor);
        void applyLightProperties(AActor* actor, const LightSettings& settings);

        std::vector<LightEntry> lightEntries;
        std::mutex lightsMtx;
        AActor* pendingSelection = nullptr;
        AActor* selectedLight = nullptr;
        LightSettings selectedSettings;
        float lastRefresh = 0.0f;
};

#endif
