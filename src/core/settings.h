#ifndef SAS_SETTINGS_H
#define SAS_SETTINGS_H

#include "nlohmann/json.hpp"
#include <filesystem>
#include <string>

inline const std::string SUPPORTED_LANGUAGES[][2] = {
    {"en", "English" },
    {"fr", "français"},
    {"de", "Deutsch" },
    {"es", "Español" },
    {"it", "Italiano"},
    {"pl", "Polski"  },
    {"jp", "日本語"  },
    {"cn", "中文"    },
    {"tw", "繁體中文"}
};

struct ThemeEntry {
        const char* id;
        const char* label;
};

inline const ThemeEntry SUPPORTED_THEMES[] = {
    {"default",  "Default (dark)"     },
    {"dark",     "ImGui Dark (dark)"  },
    {"paper",    "Paper & Ink (light)"},
    {"maroon",   "Maroon (dark)"      },
    {"spectrum", "Spectrum (light)"   },
};

inline constexpr int SETTINGS_FONT_SIZE_MIN = 10;
inline constexpr int SETTINGS_FONT_SIZE_MAX = 32;
inline constexpr int SETTINGS_FONT_SIZE_DEFAULT = 14;
inline constexpr float SETTINGS_FONT_BASE_PX = 15.0f;

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

struct SettingOptions {
        std::string language = "en";
        int fontSize = SETTINGS_FONT_SIZE_DEFAULT;
        std::string theme = "default";
        std::string showOverlay = "F10";
        float freecamAdjust = SETTINGS_FREECAM_ADJUST;
        float freecamRoll = SETTINGS_FREECAM_ROLL_DEFAULT;
        float freecamFOV = SETTINGS_FREECAM_FOV_DEFAULT;

        float freecamBloomThreshold = 1.0f;
        float freecamBloomScale = 1.0f;
        float freecamDofDistance = 1.0f;
        float freecamDofInnerRadius = SETTINGS_FREECAM_DOF_INNER_RADIUS_MIN;
        float freecamDofFStop = SETTINGS_FREECAM_DOF_FSTOP_MIN;
        float freecamDofIntensity = SETTINGS_FREECAM_DOF_INTENSITY_MIN;

        float freecamSaturation = 1.0f;
        float freecamContrast = 1.5f;
        float freecamBright = 1.0f;
        float freecamSat = 1.0f;
        float freecamMoveSpeed = 1.0f;

        bool isFreecamAdjustEnabled = false;
        bool isFreecamDofEnabled = false;
        bool isFreecamBloomEnabled = false;
        bool isFreecamColorEnabled = false;

        bool isFreecamHideOthersEnabled = false;
        bool isFreecamHideSelfEnabled = false;
        bool isFreecamHidePartyEnabled = false;
        bool isFreecamHideNPCsEnabled = false;
        bool isFreecamHideVehicleEnabled = false;
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

        SettingOptions options;

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

        static bool tryVkFromName(const std::string& name, int* vkOut);
        static int vkFromName(const std::string& name);
        static std::string nameFromVk(int vk);

    private:
        static const char* configFileName() {
            return "settings.json";
        }
        void clampAndValidate();

        nlohmann::json settingsJson;
        bool didSettingsChangedBool = false;
        static Settings* settingsInstance;
};

#endif // SAS_SETTINGS_H
