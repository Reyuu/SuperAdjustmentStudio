#ifndef SAS_LIGHTS_H
#define SAS_LIGHTS_H

#include <LESDK/Includes.hpp>
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
        bool enabled = SETTINGS_LIGHT_ENABLED_DEFAULT;
        float color[3] = {SETTINGS_COLOR_WHITE_R, SETTINGS_COLOR_WHITE_G, SETTINGS_COLOR_WHITE_B};
        float brightness = SETTINGS_LIGHT_BRIGHTNESS_DEFAULT;
        float radius = SETTINGS_LIGHT_RADIUS_DEFAULT;
        float falloffExponent = SETTINGS_LIGHT_FALLOFF_DEFAULT;

        float innerConeAngle = SETTINGS_LIGHT_CONE_INNER_DEFAULT;
        float outerConeAngle = SETTINGS_LIGHT_CONE_OUTER_DEFAULT;
        float lightShaftConeAngle = SETTINGS_LIGHT_CONE_INNER_DEFAULT;

        float bloomScale = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomThreshold = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomScreenBlendThreshold = SETTINGS_LIGHT_BLOOM_DEFAULT;
        float bloomTint[3] = {SETTINGS_COLOR_WHITE_R, SETTINGS_COLOR_WHITE_G, SETTINGS_COLOR_WHITE_B};

        float shadowRadiusMultiplier = SETTINGS_LIGHT_SHADOW_RADIUS_MULT_DEFAULT;
        int shadowProjectionTechnique = SETTINGS_LIGHT_SHADOW_PROJ_DEFAULT;
        int shadowFilterQuality = SETTINGS_LIGHT_SHADOW_FILTER_DEFAULT;
        int lightShadowMode = SETTINGS_LIGHT_SHADOW_MODE_DEFAULT;
        bool castShadows = SETTINGS_LIGHT_CAST_SHADOWS_DEFAULT;
        bool castDynamicShadows = SETTINGS_LIGHT_CAST_DYNAMIC_SHADOWS_DEFAULT;

        bool renderLightShafts = SETTINGS_LIGHT_RENDER_SHAFTS_DEFAULT;
        bool isPoint = SETTINGS_TOGGLE_OFF;
        bool isSpot = SETTINGS_TOGGLE_OFF;
};

class LightManager {
    public:
        void renderUI();
        static bool readLightSettings(AActor* actor, LightSettings& out);
        void addLight(AActor* actor, const std::string& type);
        void removeAllLights();
        void applyLightProperties(AActor* actor, const LightSettings& settings);

        std::vector<LightEntry> lightEntries;
        std::mutex lightsMtx;

    private:
        void removeLight(AActor* actor);
        void updateActiveLights();
        void selectLight(AActor* actor);
        AActor* pendingSelection = nullptr;
        AActor* selectedLight = nullptr;
        LightSettings selectedSettings;
        float lastRefresh = 0.0f;
};

#endif
