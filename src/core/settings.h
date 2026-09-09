#ifndef SAS_SETTINGS_H
#define SAS_SETTINGS_H

#include "json.hpp"
#include <filesystem>
#include <string>

inline const std::string SUPPORTED_LANGUAGES[][2] = {
    {"en",    "English" },
    {"fr",    "français"},
    {"de",    "Deutsch" },
    {"es-ES", "Español" },
    {"it",    "Italiano"},
    {"pl",    "Polski"  },
    {"ja",    "日本語"  },
    {"zh-CN", "中文"    },
    {"zh-TW", "繁體中文"}
};

struct ThemeEntry {
        const char* id;
        const char* label;
};

inline const ThemeEntry SUPPORTED_THEMES[] = {
    {"default",  "Default (dark)"     },
    {"dark",     "ImGui Dark (dark)"  },
    {"maroon",   "Maroon (dark)"      },
    {"paper",    "Paper & Ink (light)"},
    {"spectrum", "Spectrum (light)"   },
};

inline constexpr int SETTINGS_FONT_SIZE_MIN = 10;
inline constexpr int SETTINGS_FONT_SIZE_MAX = 32;
inline constexpr int SETTINGS_FONT_SIZE_DEFAULT = 14;
inline constexpr float SETTINGS_FONT_BASE_PX = 15.0f;
inline constexpr float SETTINGS_FONT_SCALE_MIN = 0.5f;
inline constexpr float SETTINGS_FONT_SCALE_MAX = 3.0f;

inline constexpr float SETTINGS_FREECAM_ADJUST = 1.0f;
inline constexpr float SETTINGS_FREECAM_ROLL_DEFAULT = 0.0f;
inline constexpr float SETTINGS_FREECAM_ROLL_MIN = -180.0f;
inline constexpr float SETTINGS_FREECAM_ROLL_MAX = 180.0f;
inline constexpr float SETTINGS_FREECAM_FOV_DEFAULT = 75.0f;
inline constexpr float SETTINGS_FREECAM_FOV_MIN = 10.0f;
inline constexpr float SETTINGS_FREECAM_FOV_MAX = 180.0f;
inline constexpr float SETTINGS_FREECAM_LOOK_DEG_PER_PX = 0.1f;
inline constexpr float SETTINGS_FREECAM_UU_PER_PX = 10.0f;

inline constexpr float SETTINGS_FREECAM_BLOOM_THRESHOLD_MIN = 0.5f;
inline constexpr float SETTINGS_FREECAM_BLOOM_SCALE_MIN = 0.0f;
inline constexpr float SETTINGS_FREECAM_DOF_DISTANCE_MIN = 0.1f;
inline constexpr float SETTINGS_FREECAM_DOF_FSTOP_MIN = 0.1f;
inline constexpr float SETTINGS_FREECAM_DOF_INNER_RADIUS_MIN = 0.1f;
inline constexpr float SETTINGS_FREECAM_DOF_INTENSITY_MIN = 0.0f;
inline constexpr float SETTINGS_FREECAM_SATURATION_MIN = 0.0f;
inline constexpr float SETTINGS_FREECAM_MOVE_SPEED_MIN = 0.0f;
inline constexpr float SETTINGS_FREECAM_CONTRAST_MIN = 1.5f;
inline constexpr float SETTINGS_FREECAM_COLOR_MIN = 0.0f;

inline constexpr float SETTINGS_FREECAM_BLOOM_THRESHOLD_MAX = 5.0f;
inline constexpr float SETTINGS_FREECAM_BLOOM_SCALE_MAX = 5.0f;
inline constexpr float SETTINGS_FREECAM_DOF_DISTANCE_MAX = 2000.0f;
inline constexpr float SETTINGS_FREECAM_DOF_FSTOP_MAX = 100.0f;
inline constexpr float SETTINGS_FREECAM_DOF_INNER_RADIUS_MAX = 100.0f;
inline constexpr float SETTINGS_FREECAM_DOF_INTENSITY_MAX = 4.0f;
inline constexpr float SETTINGS_FREECAM_SATURATION_MAX = 2.0f;
inline constexpr float SETTINGS_FREECAM_MOVE_SPEED_MAX = 100.0f;
inline constexpr float SETTINGS_FREECAM_CONTRAST_MAX = 2.5f;
inline constexpr float SETTINGS_FREECAM_COLOR_MAX = 2.0f;

inline constexpr float SETTINGS_FREECAM_MOVE_SPEED_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_BLOOM_THRESHOLD_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_BLOOM_SCALE_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_DOF_DISTANCE_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_DOF_INNER_RADIUS_DEFAULT = SETTINGS_FREECAM_DOF_INNER_RADIUS_MIN;
inline constexpr float SETTINGS_FREECAM_DOF_FSTOP_DEFAULT = SETTINGS_FREECAM_DOF_FSTOP_MIN;
inline constexpr float SETTINGS_FREECAM_DOF_INTENSITY_DEFAULT = SETTINGS_FREECAM_DOF_INTENSITY_MIN;
inline constexpr float SETTINGS_FREECAM_SATURATION_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_CONTRAST_DEFAULT = 1.5f;
inline constexpr float SETTINGS_FREECAM_BRIGHT_DEFAULT = 1.0f;
inline constexpr float SETTINGS_FREECAM_SAT_DEFAULT = 1.0f;

inline constexpr float SETTINGS_FREECAM_DOF_INTENSITY_DIV = 4.0f;
inline constexpr float SETTINGS_FREECAM_CONTRAST_PIVOT = 2.0f;

inline constexpr float SETTINGS_PHOTO_MASK_OPACITY_DEFAULT = 0.6f;
inline constexpr float SETTINGS_PHOTO_MASK_OPACITY_MIN = 0.0f;
inline constexpr float SETTINGS_PHOTO_MASK_OPACITY_MAX = 1.0f;
inline constexpr float SETTINGS_PHOTO_LINE_THICKNESS_DEFAULT = 1.5f;
inline constexpr float SETTINGS_PHOTO_LINE_THICKNESS_MIN = 0.0f;
inline constexpr float SETTINGS_PHOTO_LINE_THICKNESS_MAX = 5.0f;
inline constexpr int SETTINGS_PHOTO_CLIP_LO_DEFAULT = 8;
inline constexpr int SETTINGS_PHOTO_CLIP_HI_DEFAULT = 247;
inline constexpr int SETTINGS_PHOTO_CLIP_MIN = 0;
inline constexpr int SETTINGS_PHOTO_CLIP_MAX = 255;
inline constexpr float SETTINGS_PHOTO_TINT_STRENGTH_DEFAULT = 0.0f;
inline constexpr float SETTINGS_PHOTO_TINT_STRENGTH_MIN = 0.0f;
inline constexpr float SETTINGS_PHOTO_TINT_STRENGTH_MAX = 1.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_INTENSITY_DEFAULT = 0.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_INTENSITY_MIN = 0.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_INTENSITY_MAX = 1.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_OPACITY_DEFAULT = 1.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_OPACITY_MIN = 0.0f;
inline constexpr float SETTINGS_PHOTO_GRAIN_OPACITY_MAX = 1.0f;

inline constexpr float SETTINGS_LIGHT_BRIGHTNESS_DEFAULT = 1.0f;
inline constexpr float SETTINGS_LIGHT_BRIGHTNESS_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_BRIGHTNESS_MAX = 100.0f;
inline constexpr float SETTINGS_LIGHT_RADIUS_DEFAULT = 1024.0f;
inline constexpr float SETTINGS_LIGHT_RADIUS_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_RADIUS_MAX = 10000.0f;
inline constexpr float SETTINGS_LIGHT_FALLOFF_DEFAULT = 2.0f;
inline constexpr float SETTINGS_LIGHT_FALLOFF_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_FALLOFF_MAX = 16.0f;
inline constexpr float SETTINGS_LIGHT_SHADOW_RADIUS_MULT_DEFAULT = 1.0f;
inline constexpr float SETTINGS_LIGHT_SHADOW_RADIUS_MULT_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_SHADOW_RADIUS_MULT_MAX = 16.0f;
inline constexpr float SETTINGS_LIGHT_CONE_INNER_DEFAULT = 0.0f;
inline constexpr float SETTINGS_LIGHT_CONE_OUTER_DEFAULT = 44.0f;
inline constexpr float SETTINGS_LIGHT_CONE_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_CONE_MAX = 89.0f;
inline constexpr float SETTINGS_LIGHT_BLOOM_DEFAULT = 0.0f;
inline constexpr float SETTINGS_LIGHT_BLOOM_MIN = 0.0f;
inline constexpr float SETTINGS_LIGHT_BLOOM_MAX = 100.0f;

inline constexpr float SETTINGS_PREFAB_BRIGHTNESS_DEFAULT = 0.4f;
inline constexpr float SETTINGS_PREFAB_BRIGHTNESS_MIN = 0.0f;
inline constexpr float SETTINGS_PREFAB_BRIGHTNESS_MAX = 20.0f;
inline constexpr float SETTINGS_PREFAB_RADIUS_DEFAULT = 350.0f;
inline constexpr float SETTINGS_PREFAB_RADIUS_MIN = 0.0f;
inline constexpr float SETTINGS_PREFAB_RADIUS_MAX = 3000.0f;
inline constexpr float SETTINGS_PREFAB_EXPANSION_DEFAULT = 95.0f;
inline constexpr float SETTINGS_PREFAB_EXPANSION_MIN = -200.0f;
inline constexpr float SETTINGS_PREFAB_EXPANSION_MAX = 500.0f;

inline constexpr float SETTINGS_FX_LOOP_DELAY_DEFAULT = 0.0f;
inline constexpr float SETTINGS_FX_LOOP_DELAY_MIN = 0.0f;
inline constexpr float SETTINGS_FX_LOOP_DELAY_MAX = 60.0f;
inline constexpr float SETTINGS_FX_DURATION_DEFAULT = 10.0f;
inline constexpr float SETTINGS_FX_DURATION_MIN = 0.1f;
inline constexpr float SETTINGS_FX_DURATION_MAX = 60.0f;

inline constexpr bool SETTINGS_TOGGLE_OFF = false;
inline constexpr bool SETTINGS_LIGHT_ENABLED_DEFAULT = true;
inline constexpr bool SETTINGS_LIGHT_CAST_SHADOWS_DEFAULT = true;
inline constexpr bool SETTINGS_LIGHT_CAST_DYNAMIC_SHADOWS_DEFAULT = true;
inline constexpr bool SETTINGS_LIGHT_RENDER_SHAFTS_DEFAULT = false;

inline constexpr float SETTINGS_COLOR_WHITE_R = 1.0f;
inline constexpr float SETTINGS_COLOR_WHITE_G = 1.0f;
inline constexpr float SETTINGS_COLOR_WHITE_B = 1.0f;
inline constexpr float SETTINGS_PHOTO_LINE_ALPHA_DEFAULT = 0.85f;

inline constexpr int SETTINGS_PHOTO_GRID_DEFAULT = 0;
inline constexpr int SETTINGS_PHOTO_ASPECT_DEFAULT = 0;
inline constexpr int SETTINGS_LIGHT_SHADOW_PROJ_DEFAULT = 0;
inline constexpr int SETTINGS_LIGHT_SHADOW_FILTER_DEFAULT = 0;
inline constexpr int SETTINGS_LIGHT_SHADOW_MODE_DEFAULT = 0;

inline constexpr int SETTINGS_SHOT_MULTIPLIER_MIN = 1;
inline constexpr int SETTINGS_SHOT_MULTIPLIER_MAX = 8;
inline constexpr int SETTINGS_SHOT_MULTIPLIER_DEFAULT = 2;
inline constexpr int SETTINGS_SHOT_OVERLAP_MIN = 0;
inline constexpr int SETTINGS_SHOT_OVERLAP_MAX = 1000;
inline constexpr int SETTINGS_SHOT_OVERLAP_DEFAULT = 250;

struct SettingsOptions {
        std::string language = "en";
        int fontSize = SETTINGS_FONT_SIZE_DEFAULT;
        std::string theme = "default";
        std::string showOverlay = "F10";
        float freecamAdjust = SETTINGS_FREECAM_ADJUST;
        float freecamRoll = SETTINGS_FREECAM_ROLL_DEFAULT;
        float freecamFOV = SETTINGS_FREECAM_FOV_DEFAULT;

        float freecamBloomThreshold = SETTINGS_FREECAM_BLOOM_THRESHOLD_DEFAULT;
        float freecamBloomScale = SETTINGS_FREECAM_BLOOM_SCALE_DEFAULT;
        float freecamDofDistance = SETTINGS_FREECAM_DOF_DISTANCE_DEFAULT;
        float freecamDofInnerRadius = SETTINGS_FREECAM_DOF_INNER_RADIUS_DEFAULT;
        float freecamDofFStop = SETTINGS_FREECAM_DOF_FSTOP_DEFAULT;
        float freecamDofIntensity = SETTINGS_FREECAM_DOF_INTENSITY_DEFAULT;

        float freecamSaturation = SETTINGS_FREECAM_SATURATION_DEFAULT;
        float freecamContrast = SETTINGS_FREECAM_CONTRAST_DEFAULT;
        float freecamBright = SETTINGS_FREECAM_BRIGHT_DEFAULT;
        float freecamSat = SETTINGS_FREECAM_SAT_DEFAULT;
        float freecamMoveSpeed = SETTINGS_FREECAM_MOVE_SPEED_DEFAULT;

        bool isFreecamAdjustEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamDofEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamBloomEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamColorEnabled = SETTINGS_TOGGLE_OFF;

        bool isFreecamHideOthersEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamHideSelfEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamHidePartyEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamHideNPCsEnabled = SETTINGS_TOGGLE_OFF;
        bool isFreecamHideVehicleEnabled = SETTINGS_TOGGLE_OFF;

        int shotMultiplier = SETTINGS_SHOT_MULTIPLIER_DEFAULT;
        int shotOverlap = SETTINGS_SHOT_OVERLAP_DEFAULT;
        std::string shotFormat = "png";
        std::string shotSaveDir;
        bool shotExtraUnlit = SETTINGS_TOGGLE_OFF;
};

struct OverlayHotkey {
    public:
        bool ctrl = SETTINGS_TOGGLE_OFF;
        bool alt = SETTINGS_TOGGLE_OFF;
        bool shift = SETTINGS_TOGGLE_OFF;
        int key = 0x79; // F10 by default
};

class Settings {
    public:
        Settings();
        ~Settings();

        static Settings& instance() {
            return *settingsInstance;
        }
        static bool hasInstance() {
            return settingsInstance != nullptr;
        }

        SettingsOptions options;

        void loadFromJson(const nlohmann::json& j);
        nlohmann::json toJson() const;

        bool isValidLanguage(const std::string& lang) const;
        bool isValidTheme(const std::string& th) const;

        bool didSettingsChange() const {
            return didSettingsChangedBool;
        }
        void markChanged() {
            didSettingsChangedBool = true;
        }

        bool loadSettings(const nlohmann::json& j);
        bool saveSettings(nlohmann::json* j);

        bool load();
        bool save();
        void revert();

        std::filesystem::path configDir() const;
        std::filesystem::path configPath() const;

        void renderSettingsWindow(bool* open);

        static bool isModifierKey(int vk);
        static void queryModdifiers(bool* ctrl, bool* alt, bool* shift);
        static bool tryParseHotkey(const std::string& str, OverlayHotkey* hotkey);
        static std::string formatHotkey(const OverlayHotkey& hotkey);

        static bool tryVkFromName(const std::string& name, int* vkOut);
        static int vkFromName(const std::string& name);
        static std::string nameFromVk(int vk);

        bool capturingHotkey = false;

    private:
        bool capturePrevDown[256] = {};
        static const char* configFileName() {
            return "settings.json";
        }
        void clampAndValidate();

        nlohmann::json settingsJson;
        bool didSettingsChangedBool = false;
        static Settings* settingsInstance;
};

#endif // SAS_SETTINGS_H
