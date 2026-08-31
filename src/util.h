#ifndef SAS_UTIL_H
#define SAS_UTIL_H

#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "imgui.h"
#include <LESDK/Includes.LE2.hpp>
#include <string>

// UE3 -> transform in degrees for rotation and per-axis scale. Converted to whatever is usable at the engine.
struct Transform {
    public:
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float rot[3] = {0.0f, 0.0f, 0.0f};
        float scale[3] = {1.0f, 1.0f, 1.0f};
};

// UE3 EClassFlags (verified against LE2 class flag dumps; these are the real values for LE2 engine build).
// Note: Standard UE3 enum values (e.g. UT3) differ from BioWare's LE2 engine build bit assignments.
enum EClassFlags : DWORD {
    CLASS_None = 0x00000000,
    CLASS_Abstract = 0x00000001,
    CLASS_NotPlaceable = 0x00000200,
    CLASS_Deprecated = 0x00020000,
    CLASS_Placeable = 0x00400000,
    CLASS_Hidden = 0x04000000,
};

std::string FStringToUtf8(const FString& fStr);
std::string WStringToUtf8(const std::wstring& wStr);
std::wstring toWString(const std::string& str);
std::string toLowerStr(const std::string& str);
std::vector<std::string> collectGamePccFiles();

// first live object of type T in GObjObjects
template <class T> T* findFirstOf() {
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (o && o->IsA(T::StaticClass())) {
            return static_cast<T*>(o);
        }
    }
    return nullptr;
}

// run fn over every live object of type T in GObjObjects
template <class T, class Fn> void forEachOf(Fn&& fn) {
    if (!UObject::GObjObjects) {
        return;
    }
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (o && o->IsA(T::StaticClass())) {
            fn(static_cast<T*>(o));
        }
    }
}

static ImVec4 hexToImVec4(const std::string& hex) {
    // remove leading '#' if present
    std::string hexStr = hex;
    if (!hexStr.empty() && hexStr[0] == '#') {
        hexStr = hexStr.substr(1);
    }

    if (hex.size() != 6 && hex.size() != 8) {
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    unsigned int r = 255, g = 255, b = 255, a = 255;
    if (hex.size() == 6) {
        sscanf_s(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
    } else {
        sscanf_s(hex.c_str(), "%02x%02x%02x%02x", &r, &g, &b, &a);
    }
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

static bool isLiveObject(UObject* obj) {
    if (!obj || !UObject::GObjObjects || !UObject::GObjObjects->GetData()) {
        return false;
    }
    if (obj->Class == nullptr) {
        return false;
    }
    UObject** data = UObject::GObjObjects->GetData();
    const int n = (int)UObject::GObjObjects->Count();
    for (int i = 0; i < n; ++i) {
        if (data[i] == obj) {
            return true;
        }
    }
    return false;
}

static void LoadPackageByName(const std::string& packageName) {
}

#endif // SAS_UTIL_H