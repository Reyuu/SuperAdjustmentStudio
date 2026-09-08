#ifndef SAS_LIGHTS_H
#define SAS_LIGHTS_H

#include <LESDK/Includes.LE2.hpp>
#include <mutex>
#include <string>
#include <vector>
#include "settings.h"

// tried to implement pointlight, but pointlight is a bitch to spawn.

struct LightEntry {
        std::string name;
        std::string type;
        AActor* actor = nullptr;
};

struct LightSettings {
        bool enabled = true;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float brightness = SETTINGS_LIGHT_BRIGHTNESS_DEFAULT;
        float radius = SETTINGS_LIGHT_RADIUS_DEFAULT;
        float falloffExponent = SETTINGS_LIGHT_FALLOFF_DEFAULT;

        float innerConeAngle = SETTINGS_LIGHT_CONE_INNER_DEFAULT;
        float outerConeAngle = SETTINGS_LIGHT_CONE_OUTER_DEFAULT;
        float lightShaftConeAngle = SETTINGS_LIGHT_CONE_INNER_DEFAULT;

        float bloomScale = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomThreshold = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomScreenBlendThreshold = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomTint[3] = {1.0f, 1.0f, 1.0f};

        float shadowRadiusMultiplier = SETTINGS_LIGHT_SHADOW_RADIUS_MULT_DEFAULT;
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
