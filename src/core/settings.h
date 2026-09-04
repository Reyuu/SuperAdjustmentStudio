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

struct SettingOptions {
        std::string language = "en";
        int fontSize = SETTINGS_FONT_SIZE_DEFAULT;
        std::string theme = "default";
        std::string showOverlay = "F10";
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
