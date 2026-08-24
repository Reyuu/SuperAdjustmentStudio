#ifndef SAS_UTIL_H
#define SAS_UTIL_H

#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <string>
#include <LESDK/Includes.LE2.hpp>


//UE3 -> transform in degrees for rotation and per-axis scale. Converted to whatever is usable at the engine.
struct Transform {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float rot[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

// UE3 EClassFlags (verified against LE2 class flag dumps; these are the real values).
static constexpr DWORD CLASS_Abstract    = 0x00000001;
static constexpr DWORD CLASS_NotPlaceable = 0x00000200;
static constexpr DWORD CLASS_Placeable   = 0x00400000;
static constexpr DWORD CLASS_Hidden      = 0x04000000;
static constexpr DWORD CLASS_Deprecated  = 0x00020000;

std::string FStringToUtf8(const FString& fStr);
std::string WStringToUtf8(const std::wstring& wStr);
std::wstring toWString(const std::string& str);
std::string toLowerStr(const std::string& str);

// first live object of type T in GObjObjects
template<class T>
T* findFirstOf() {
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
template<class T, class Fn>
void forEachOf(Fn&& fn) {
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

#endif // SAS_UTIL_H