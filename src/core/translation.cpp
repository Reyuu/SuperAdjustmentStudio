#include "translation.h"
#include "logger.h"

#include <string>
#include <fstream>
#include <sstream>
#include <cstdarg>
#include <windows.h>

#define IDR_TRANSLATIONS 102

Translation* Translation::translationInstance = nullptr;

Translation::Translation() {
    translationInstance = this;
    loadTranslations();
}

Translation::~Translation() {
    if (translationInstance == this) {
        translationInstance = nullptr;
    }
}

// get translation for the given key in the current language, using dot notation for nested keys
static bool walkTranslationKey(const json& root, const std::string& key, std::string& out) {
    const json* node = &root;
    size_t start = 0;

    while (true) {
        size_t dot = key.find('.', start);
        std::string part = key.substr(start, dot - start);
        if (!node->contains(part)) {
            return false;
        }

        node = &(*node)[part];

        if (dot == std::string::npos) {
            break;
        }

        start = dot + 1;
    }

    if (!node->is_string()) {
        return false;
    }

    out = node->get<std::string>();
    return true;
}

std::string Translation::translate(const std::string& key) {
    std::string lang = currentLanguage;

    if (!translations.contains(lang)) {
        lang = "en"; // fallback to English if the current language is not available
    }
    if (!translations.contains(lang)) {
        return key;
    }

    std::string out;
    if (walkTranslationKey(translations.at(lang), key, out)) {
        return out;
    }
    // per-key fallback to English so untranslated keys still read sensibly
    if (lang != "en" && translations.contains("en") && walkTranslationKey(translations.at("en"), key, out)) {
        return out;
    }
    return key;
}

void Translation::setLanguage(const std::string& language) {
    currentLanguage = language;
}

static void translation_anchor() {
}

void Translation::loadTranslations() {
    if (currentLanguage.empty()) {
        Logger->error("translation: current language is empty.");
        return;
    }

    // load translations from the JSON file for the current language
    // get the translations for the current language from the JSON file in included resources
    HMODULE hModule = nullptr;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&translation_anchor, &hModule);
    if (!hModule) {
        Logger->error("translation: failed to get module handle.");
        return;
    }

    HRSRC hResource = FindResourceA(hModule, MAKEINTRESOURCE(IDR_TRANSLATIONS), RT_RCDATA);
    if (!hResource) {
        Logger->error("translation: failed to find translation resource.");
        return;
    }

    HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
    if (!hLoadedResource) {
        Logger->error("translation: failed to load translation resource.");
        return;
    }

    LPVOID pLockedResource = LockResource(hLoadedResource);
    if (!pLockedResource) {
        Logger->error("translation: failed to lock translation resource.");
        return;
    }

    std::string jsonData(static_cast<char*>(pLockedResource), SizeofResource(hModule, hResource));
    if (jsonData.empty()) {
        Logger->error("translation: translation resource is empty.");
        return;
    }

    std::istringstream file(jsonData);
    if (!file) {
        Logger->error("translation: failed to create input stream for translation resource.");
        return;
    }

    file >> translations;
    Logger->info("translation: translations loaded successfully.");
}