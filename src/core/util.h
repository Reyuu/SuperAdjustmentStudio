#ifndef SAS_UTIL_H
#define SAS_UTIL_H

#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "imgui.h"
#include <LESDK/Includes.hpp>
#include <LESDK/Common/Math.hpp>
#include <numbers>
#include <string>

// folder picker (existing callers). opens in the last-used shell location.
bool pickFolder(std::string& outPath);

// single file picker. filter uses the common dialog convention
bool pickFile(std::string& outPath, const char* filter);

// UE3 -> transform in degrees for rotation and per-axis scale. Converted to whatever is usable at the engine.
struct Transform {
    public:
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float rot[3] = {0.0f, 0.0f, 0.0f};
        float scale[3] = {1.0f, 1.0f, 1.0f};
};

// the bits are per engine build, not per UE3, so they cannot be shared. the LE2 block
// is verified against LE2 dumps, the LE1 block is seeded with the standard UE3 values
// and is NOT verified: spawnClass only uses it after an unmodified Spawn came back NULL
#ifdef SDK_TARGET_LE1
enum EClassFlags : DWORD {
    CLASS_None = 0x00000000,
    CLASS_Abstract = 0x00000001,
    CLASS_NotPlaceable = 0x00000008,
    CLASS_Deprecated = 0x00020000,
    CLASS_Placeable = 0x00000040,
    CLASS_Hidden = 0x04000000,
};
#else
// verified against LE2 dumps, these are the real LE2 build values
enum EClassFlags : DWORD {
    CLASS_None = 0x00000000,
    CLASS_Abstract = 0x00000001,
    CLASS_NotPlaceable = 0x00000200,
    CLASS_Deprecated = 0x00020000,
    CLASS_Placeable = 0x00400000,
    CLASS_Hidden = 0x04000000,
};
#endif

std::string FStringToUtf8(const FString& fStr);
std::string WStringToUtf8(const std::wstring& wStr);
std::wstring toWString(const std::string& str);
std::string toLowerStr(const std::string& str);
struct PccFile {
        std::string path;    // absolute path
        std::string relPath; // path relative to BIOGame
};
std::vector<PccFile> collectGamePccFiles();

// GObjObjects is mutated by the game thread (GC) while the render thread scans it,
// so an entry's Class pointer can be stale and IsA walks Class->SuperField into freed
// memory. the test must be SEH-guarded (no local objects, so __try is legal here).
static bool safeIsA(UObject* o, UClass* cls) {
    __try {
        return o->IsA(cls);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// first live object of type T in GObjObjects
template <class T> T* findFirstOf() {
    if (!UObject::GObjObjects) {
        return nullptr;
    }
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        UObject* o = UObject::GObjObjects->GetData()[i];
        if (o && safeIsA(o, T::StaticClass())) {
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
        if (o && safeIsA(o, T::StaticClass())) {
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
    // match by pointer identity first: obj may be a dangling pointer, so it must
    // not be dereferenced until it is known to be a live object
    UObject** data = UObject::GObjObjects->GetData();
    const int n = (int)UObject::GObjObjects->Count();
    for (int i = 0; i < n; ++i) {
        if (data[i] == obj) {
            return obj->Class != nullptr;
        }
    }
    return false;
}

static void LoadPackageByName(const std::string& packageName) {
}

static FVector RotateVector(const FVector& v, const FRotator& r) {
    float pitch = UnrealRotationUnitsToRadians(r.Pitch);
    float yaw = UnrealRotationUnitsToRadians(r.Yaw);
    float roll = UnrealRotationUnitsToRadians(r.Roll);
    float SP = sinf(pitch), CP = cosf(pitch);
    float SY = sinf(yaw), CY = cosf(yaw);
    float SR = sinf(roll), CR = cosf(roll);

    // i have no idea... i just work here...
    FVector out;
    out.X = v.X * (CP * CY) + v.Y * (CP * SY) + v.Z * SP;
    out.Y = v.X * (SR * SP * CY - CR * SY) + v.Y * (SR * SP * SY + CR * CY) + v.Z * (-SR * CP);
    out.Z = v.X * (-(CR * SP * CY + SR * SY)) + v.Y * (-(CR * SP * SY - SR * CY)) + v.Z * (CR * CP);
    return out;
}

static std::string FStringToString(const FString& fstr) {
    std::string result = "";
    for (int i = 0; i < fstr.Length(); ++i) {
        result += static_cast<char>(fstr.Chars()[i]);
    }
    return result;
}

constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;
constexpr float MaxInPitchRadians = 89.0f * DegreesToRadians;

inline FVector RotateAboutUnit(FVector v, FVector axis, float angleRadians) {
    const float c = cosf(angleRadians);
    const float s = sinf(angleRadians);
    return v * c + Cross(axis, v) * s + axis * Dot(axis, v) * (1.0f - c);
}

inline FMatrix ViewBasisFromRotatorRoll(const FRotator& r, int rollUnit) {
    return MatrixCompose(FVector{0.0f, 0.0f, 0.0f}, FVector{1.0f, 1.0f, 1.0f}, UnrealRotationUnitsToRadians(r.Pitch), UnrealRotationUnitsToRadians(r.Yaw),
                         UnrealRotationUnitsToRadians(rollUnit));
}

#endif // SAS_UTIL_H