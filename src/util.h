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

#endif // SAS_UTIL_H