#include "util.h"
#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>
#include <ShlObj.h>
#include <wrl/client.h>

#include <LESDK/Common/Math.hpp>
#include <LESDK/Includes.hpp>

// shared body for pickFolder/pickFile
static bool pickPathWithDialog(std::string& outPath, DWORD extraOptions, const char* filter) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileDialog> fileDialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fileDialog));

    if (FAILED(hr)) {
        return false;
    }

    DWORD options = 0;
    fileDialog->GetOptions(&options);
    fileDialog->SetOptions(options | extraOptions | FOS_FORCEFILESYSTEM);

    if (filter && filter[0]) {
        // common dialog convention: "display name|pattern". without the bar the
        // whole string serves as both name and pattern.
        const std::string spec(filter);
        std::string filterName;
        std::string filterPattern;
        const size_t bar = spec.find('|');
        if (bar == std::string::npos) {
            filterName = spec;
            filterPattern = spec;
        } else {
            filterName = spec.substr(0, bar);
            filterPattern = spec.substr(bar + 1);
        }
        COMDLG_FILTERSPEC filterSpec = {};
        const std::wstring filterNameW = toWString(filterName);
        const std::wstring filterPatternW = toWString(filterPattern);
        filterSpec.pszName = filterNameW.c_str();
        filterSpec.pszSpec = filterPatternW.c_str();
        fileDialog->SetFileTypes(1, &filterSpec);
    }

    hr = fileDialog->Show(nullptr);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IShellItem> item;
    hr = fileDialog->GetResult(&item);
    if (FAILED(hr)) {
        return false;
    }

    PWSTR pickedPath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pickedPath);
    if (FAILED(hr) || !pickedPath) {
        return false;
    }

    outPath = WStringToUtf8(pickedPath);
    CoTaskMemFree(pickedPath);
    if (outPath.empty()) {
        return false;
    }
    return true;
}

bool pickFolder(std::string& outPath) {
    return pickPathWithDialog(outPath, FOS_PICKFOLDERS | FOS_PATHMUSTEXIST, nullptr);
}

bool pickFile(std::string& outPath, const char* filter) {
    return pickPathWithDialog(outPath, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST, filter);
}

// UE3 FString -> length-prefixed UTF-16 buffer
std::string FStringToUtf8(const FString& fStr) {
    std::wstring wStr = (std::wstring)fStr;
    if (wStr.empty()) {
        return {};
    }

    UINT n = LESDK::GetUtf8LengthWide(wStr.c_str(), (UINT)wStr.size());
    if (n == 0) {
        return {};
    }

    std::string out(n, '\0');
    DWORD err = 0;
    if (!LESDK::EncodeUtf8FromWide(wStr.c_str(), (UINT)wStr.size(), &out[0], n, &err)) {
        return {};
    }

    return out;
};

std::string WStringToUtf8(const std::wstring& wStr) {
    if (wStr.empty()) {
        return {};
    }

    UINT n = LESDK::GetUtf8LengthWide(wStr.c_str(), (UINT)wStr.size());
    if (n == 0) {
        return {};
    }

    std::string out(n, L'\0');
    DWORD err = 0;
    if (!LESDK::EncodeUtf8FromWide(wStr.c_str(), (UINT)wStr.size(), &out[0], n, &err)) {
        return {};
    }
    return out;
};

std::wstring toWString(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    UINT n = LESDK::GetWideLengthUtf8(str.c_str(), (UINT)str.size());
    if (n == 0) {
        return {};
    }

    std::wstring out(n, L'\0');
    DWORD err = 0;
    if (!LESDK::EncodeWideFromUtf8(str.c_str(), (UINT)str.size(), &out[0], n, &err)) {
        return {};
    }
    return out;
};

std::string toLowerStr(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
};

std::vector<PccFile> collectGamePccFiles() {
    std::vector<PccFile> files;

    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0) {
        return files;
    }

    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::path bioGame;
    for (auto dir = exeDir; !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "BIOGame")) {
            bioGame = dir / "BIOGame";
            break;
        }
    }
    if (bioGame.empty()) {
        return files;
    }

    auto addPccs = [&files, &bioGame](const std::filesystem::path& dir) {
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            if (toLowerStr(entry.path().extension().string()) == ".pcc") {
                std::string full = entry.path().string();
                std::string rel = std::filesystem::relative(entry.path(), bioGame, ec).generic_string();
                if (rel.empty() || ec) {
                    rel = entry.path().filename().string();
                }
                files.push_back({full, rel});
            }
        }
    };

    addPccs(bioGame / "CookedPCConsole");

    std::error_code ec;
    std::filesystem::path dlcRoot = bioGame / "DLC";
    if (std::filesystem::exists(dlcRoot)) {
        for (auto& dlcDir : std::filesystem::directory_iterator(dlcRoot, ec)) {
            if (ec || !dlcDir.is_directory()) {
                continue;
            }
            addPccs(dlcDir.path() / "CookedPCConsole");
        }
    }

    return files;
}