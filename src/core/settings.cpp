#include "settings.h"
#include "application.h"
#include "logger.h"
#include "nlohmann/json.hpp"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <windows.h>

Settings* Settings::settingsInstance = nullptr;

Settings::Settings() {
    settingsInstance = this;
}

Settings::~Settings() {
    if (settingsInstance == this) {
        settingsInstance = nullptr;
    }
}

static bool isSpaceChar(unsigned char c) {
    return std::isspace(c) != 0;
}

static void removeSeparators(std::string& s) {
    std::string result;
    for (char c : s) {
        if (c != ' ' && c != '-' && c != '_') {
            result += c;
        }
    }
    s = result;
}

static void trimLeft(std::string& s) {
    size_t start = 0;
    while (start < s.size() && isSpaceChar((unsigned char)s[start])) {
        start++;
    }
    s.erase(0, start);
}

static void trimRight(std::string& s) {
    size_t end = s.size();
    while (end > 0 && isSpaceChar((unsigned char)s[end - 1])) {
        end--;
    }
    s.erase(end);
}

static void toUpperInPlace(std::string& s) {
    for (char& c : s) {
        c = (char)std::toupper((unsigned char)c);
    }
}

static std::string trimUpper(std::string s) {
    trimLeft(s);
    trimRight(s);
    toUpperInPlace(s);
    return s;
}

bool Settings::isValidLanguage(const std::string& lang) const {
    for (const auto& l : SUPPORTED_LANGUAGES) {
        if (l[0] == lang) {
            return true;
        }
    }
    return false;
}

bool Settings::isValidTheme(const std::string& th) const {
    for (const auto& t : SUPPORTED_THEMES) {
        if (t.id == th) {
            return true;
        }
    }
    return false;
}

void Settings::clampAndValidate() {
    if (!isValidLanguage(options.language)) {
        options.language = "en";
    }
    if (!isValidTheme(options.theme)) {
        options.theme = "default";
    }

    options.fontSize = std::clamp(options.fontSize, SETTINGS_FONT_SIZE_MIN, SETTINGS_FONT_SIZE_MAX);
    int vk = VK_F10;
    if (!tryVkFromName(options.showOverlay, &vk)) {
        options.showOverlay = "F10";
    } else {
        const std::string canonical = nameFromVk(vk);
        if (canonical.empty()) {
            options.showOverlay = "F10";
        } else {
            options.showOverlay = canonical;
        }
    }
}

void Settings::loadFromJson(const nlohmann::json& j) {
    SettingOptions next = options;
    if (j.contains("language") && j["language"].is_string()) {
        next.language = j["language"].get<std::string>();
    }

    if (j.contains("fontSize") && j["fontSize"].is_number()) {
        next.fontSize = j["fontSize"].get<int>();
    }

    if (j.contains("theme") && j["theme"].is_string()) {
        next.theme = j["theme"].get<std::string>();
    }

    if (j.contains("showOverlay") && j["showOverlay"].is_string()) {
        next.showOverlay = j["showOverlay"].get<std::string>();
    }

    if (next.language != options.language || next.fontSize != options.fontSize || next.theme != options.theme || next.showOverlay != options.showOverlay) {
        options = next;
        clampAndValidate();
        didSettingsChangedBool = true;
    } else {
        options = next;
        clampAndValidate();
    }
}

nlohmann::json Settings::toJson() const {
    nlohmann::json j;
    j["language"] = options.language;
    j["fontSize"] = options.fontSize;
    j["theme"] = options.theme;
    j["showOverlay"] = options.showOverlay;
    return j;
}

bool Settings::loadSettings(const nlohmann::json& j) {
    if (!j.is_object()) {
        return false;
    }

    settingsJson = j;
    loadFromJson(j);
    return true;
}

bool Settings::saveSettings(nlohmann::json* j) {
    if (j == nullptr) {
        return false;
    }

    *j = toJson();
    settingsJson = *j;
    didSettingsChangedBool = false;
    return true;
}

static void settingsModuleAnchor() {
}

std::filesystem::path Settings::configDir() const {
    // resolve appdata
    char appdata[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return std::filesystem::path(appdata) / "SuperAdjustmentStudio";
    }

    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&settingsModuleAnchor, &mod) &&
        mod != nullptr) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(mod, path, MAX_PATH) != 0) {
            return std::filesystem::path(path).parent_path();
        }
    }
    return std::filesystem::current_path();
}

std::filesystem::path Settings::configPath() const {
    return configDir() / configFileName();
}

bool Settings::load() {
    const std::filesystem::path path = configPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    try {
        nlohmann::json j;
        in >> j;
        settingsJson = j;
        didSettingsChangedBool = false;
        loadFromJson(j);
        didSettingsChangedBool = false;
    } catch (const std::exception& e) {
        Logger->warn("settings: failed to parse '{}': {}", path.string(), e.what());
        return false;
    }
    return true;
}

bool Settings::save() {
    const std::filesystem::path path = configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        Logger->warn("settings: cannot create '{}': {}", path.parent_path().string(), ec.message());
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        Logger->warn("settings: cannot open '{}' for writing", path.string());
        return false;
    }

    try {
        nlohmann::json j = toJson();
        out << j.dump(4);
        out.flush();
        if (!out) {
            return false;
        }
        settingsJson = j;
        didSettingsChangedBool = false;
    } catch (const std::exception& e) {
        Logger->warn("settings: failed to write '{}': {}", path.string(), e.what());
        return false;
    }
    return true;
}

void Settings::revert() {
    loadFromJson(settingsJson);
    didSettingsChangedBool = false;
}

static bool isExtendedVk(int vk) {
    switch (vk) {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS: {
            return true;
        }
        default: {
            return false;
        }
    }
}

static std::string displayNameForVk(int vk);

static std::string normalizeKeyName(const std::string& name) {
    std::string key = trimUpper(name);
    if (key == "PGUP") {
        key = "PAGEUP";
    }
    if (key == "PGDN") {
        key = "PAGEDOWN";
    }
    if (key == "DEL") {
        key = "DELETE";
    }
    if (key == "INS") {
        key = "INSERT";
    }
    if (key == "PRTSC" || key == "PRTSCR" || key == "PRINTSCREEN") {
        key = "PRINTSCREEN";
    }
    if (key == "BREAK") {
        key = "PAUSE";
    }
    if (key == "CONTROL") {
        key = "CTRL";
    }
    if (key == "RETURN") {
        key = "ENTER";
    }
    removeSeparators(key);
    return key;
}

static std::string displayNameForVk(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, (char)vk);
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk == VK_SNAPSHOT) {
        return "PRINTSCREEN";
    }
    switch (vk) {
        case VK_PAUSE:
            return "PAUSE";
        case VK_BROWSER_BACK:
            return "BROWSERBACK";
        case VK_BROWSER_FORWARD:
            return "BROWSERFORWARD";
        case VK_BROWSER_REFRESH:
            return "BROWSERREFRESH";
        case VK_BROWSER_STOP:
            return "BROWSERSTOP";
        case VK_BROWSER_SEARCH:
            return "BROWSERSEARCH";
        case VK_BROWSER_FAVORITES:
            return "BROWSERFAVORITES";
        case VK_BROWSER_HOME:
            return "BROWSERHOME";
        case VK_VOLUME_MUTE:
            return "VOLUMEMUTE";
        case VK_VOLUME_DOWN:
            return "VOLUMEDOWN";
        case VK_VOLUME_UP:
            return "VOLUMEUP";
        case VK_MEDIA_NEXT_TRACK:
            return "MEDIANEXT";
        case VK_MEDIA_PREV_TRACK:
            return "MEDIAPREV";
        case VK_MEDIA_STOP:
            return "MEDIASTOP";
        case VK_MEDIA_PLAY_PAUSE:
            return "MEDIAPLAYPAUSE";
        case VK_LAUNCH_MAIL:
            return "LAUNCHMAIL";
        case VK_LAUNCH_MEDIA_SELECT:
            return "LAUNCHMEDIA";
        case VK_LAUNCH_APP1:
            return "LAUNCHAPP1";
        case VK_LAUNCH_APP2: {
            return "LAUNCHAPP2";
        default: {
            break;
        }
        }
    }

    UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc == 0) {
        return {};
    }

    LONG lp = (LONG)(sc << 16);
    if (isExtendedVk(vk)) {
        lp |= (1L << 24);
    }

    char buf[64] = {};
    if (GetKeyNameTextA(lp, buf, (int)sizeof(buf)) == 0) {
        return {};
    }
    return normalizeKeyName(buf);
}

static std::unordered_map<std::string, int> buildVkNameTable() {
    std::unordered_map<std::string, int> table;
    for (int vk = 0x08; vk <= 0xFF; ++vk) {
        const std::string name = displayNameForVk(vk);
        if (!name.empty() && !table.contains(name)) {
            table.emplace(name, vk);
        }
    }
    return table;
}

bool Settings::tryVkFromName(const std::string& name, int* vkOut) {
    const std::string key = normalizeKeyName(name);
    if (key.empty()) {
        return false;
    }

    // handle single-character keys (letters and digits) first
    if (key.size() == 1) {
        const char c = key[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (vkOut != nullptr) {
                *vkOut = (int)c;
            }
            return true;
        }
        const SHORT scan = VkKeyScanA(c);
        if ((scan & 0xFF) == 0xFF) {
            return false;
        }
        if (vkOut != nullptr) {
            *vkOut = scan & 0xFF;
        }
        return true;
    }

    // handle function keys (F1-F24) next
    if (key[0] == 'F' && key.size() >= 2 && key.size() <= 3) {
        bool allDigits = true;
        for (size_t i = 1; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            const int n = std::atoi(key.c_str() + 1);
            if (n >= 1 && n <= 24) {
                if (vkOut != nullptr) {
                    *vkOut = VK_F1 + n - 1;
                }
                return true;
            }
        }
    }

    // handle all other keys using the prebuilt table
    static const std::unordered_map<std::string, int> table = buildVkNameTable();
    const auto it = table.find(key);
    if (it == table.end()) {
        return false;
    }
    if (vkOut != nullptr) {
        *vkOut = it->second;
    }
    return true;
}

int Settings::vkFromName(const std::string& name) {
    int vk = VK_F10;
    tryVkFromName(name, &vk);
    return vk;
}

std::string Settings::nameFromVk(int vk) {
    return displayNameForVk(vk);
}

void Settings::renderSettingsWindow(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_GEAR " Settings", open)) {
        ImGui::End();
        return;
    }

    SettingOptions& opts = options;
    auto& toasts = Application::instance().ui().toastManager;

    int langCount = 0;
    for (const auto& l : SUPPORTED_LANGUAGES) {
        (void)l;
        ++langCount;
    }
    int langIndex = 0;
    for (int i = 0; i < langCount; ++i) {
        if (SUPPORTED_LANGUAGES[i][0] == opts.language) {
            langIndex = i;
            break;
        }
    }

    ImGui::Text("Language:");
    ImGui::PushItemWidth(-100);
    std::string langPreview = SUPPORTED_LANGUAGES[langIndex][1] + " (" + SUPPORTED_LANGUAGES[langIndex][0] + ")";
    if (ImGui::BeginCombo("##settings_lang", langPreview.c_str())) {
        for (int i = 0; i < langCount; ++i) {
            const bool selected = (i == langIndex);
            std::string label = SUPPORTED_LANGUAGES[i][1] + " (" + SUPPORTED_LANGUAGES[i][0] + ")##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected)) {
                opts.language = SUPPORTED_LANGUAGES[i][0];
                markChanged();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (opts.language != "en") {
        ImGui::TextDisabled("Only English is translated for now; the choice is saved for later.");
    }

    int fontSize = opts.fontSize;
    ImGui::Text("Font size:");
    ImGui::PushItemWidth(-100);
    if (ImGui::SliderInt("##settings_font", &fontSize, SETTINGS_FONT_SIZE_MIN, SETTINGS_FONT_SIZE_MAX)) {
        opts.fontSize = std::clamp(fontSize, SETTINGS_FONT_SIZE_MIN, SETTINGS_FONT_SIZE_MAX);
        markChanged();
        Application::instance().renderer().applySettings(opts);
    }
    ImGui::PopItemWidth();

    int themeCount = (int)(sizeof(SUPPORTED_THEMES) / sizeof(SUPPORTED_THEMES[0]));
    int themeIndex = 0;
    for (int i = 0; i < themeCount; ++i) {
        if (SUPPORTED_THEMES[i].id == opts.theme) {
            themeIndex = i;
            break;
        }
    }
    ImGui::Text("Theme:");
    ImGui::PushItemWidth(-100);
    if (ImGui::BeginCombo("##settings_theme", SUPPORTED_THEMES[themeIndex].label)) {
        for (int i = 0; i < themeCount; ++i) {
            const bool selected = (i == themeIndex);
            if (ImGui::Selectable((std::string(SUPPORTED_THEMES[i].label) + "##" + std::to_string(i)).c_str(), selected)) {
                opts.theme = SUPPORTED_THEMES[i].id;
                markChanged();
                Application::instance().renderer().applySettings(opts);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    static const std::vector<std::string> allHotkeys = [] {
        std::vector<std::string> out;
        for (int n = 1; n <= 24; ++n) {
            out.push_back("F" + std::to_string(n));
        }
        for (int vk = 0x08; vk <= 0xFF; ++vk) {
            if (vk >= VK_F1 && vk <= VK_F24) {
                continue;
            }
            std::string nm = Settings::nameFromVk(vk);
            if (nm.empty()) {
                continue;
            }
            int round = 0;
            if (!Settings::tryVkFromName(nm, &round) || round != vk) {
                continue;
            }
            out.push_back(nm);
        }
        std::sort(out.begin() + 24, out.end());
        return out;
    }();
    int hotkeyIndex = 9;
    for (int i = 0; i < (int)allHotkeys.size(); ++i) {
        if (opts.showOverlay == allHotkeys[i]) {
            hotkeyIndex = i;
            break;
        }
    }
    ImGui::Text("Overlay hotkey");
    ImGui::PushItemWidth(-100);
    if (ImGui::BeginCombo("##settings_hotkey", allHotkeys[hotkeyIndex].c_str())) {
        for (int i = 0; i < (int)allHotkeys.size(); ++i) {
            const bool selected = (i == hotkeyIndex);
            if (ImGui::Selectable((allHotkeys[i] + "##" + std::to_string(i)).c_str(), selected)) {
                opts.showOverlay = allHotkeys[i];
                markChanged();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    if (didSettingsChange()) {
        ImGui::TextDisabled("(unsaved changes)");
    }
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "##settings_save")) {
        if (save()) {
            toasts.addToastNotification("Settings saved", ToastTypeSuccess, 2.0f);
        } else {
            toasts.addToastNotification("Failed to save settings (check log)", ToastTypeError, 3.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT_TO_BRACKET "##settings_revert")) {
        revert();
        Application::instance().renderer().applySettings(opts);
        toasts.addToastNotification("Settings reverted", ToastTypeInfo, 2.0f);
    }

    ImGui::End();
}
