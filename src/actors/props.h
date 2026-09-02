#ifndef SAS_PROPS_H
#define SAS_PROPS_H
#include "imgui.h"
#include "logger.h"
#include "util.h"
#include <LESDK/Includes.LE2.hpp>
#include <sstream>
#include <string>
#include <vector>

#define PROP_CASE(p, t, l)        \
    if (p->IsA(t::StaticClass())) \
        return l;

enum PropertyType : int {
    PT_FLOAT = 0, // UFloatProperty
    PT_INT,       // UIntProperty
    PT_BOOL,      // UBoolProperty
    PT_BYTE,      // UByteProperty
    PT_ENUM,      // UEnum -> complex type
    PT_NAME,      // UNameProperty
    PT_STRING,    // UStrProperty
    PT_OBJECT,    // UClassProperty, UComponentProperty, UObjectProperty
    PT_STRUCT,    // UStructProperty
    PT_ARRAY,     // UArrayProperty
    PT_MASK,      // UBioMask4Property
    PT_VECTOR,
    PT_VECTOR2D,
    PT_ROTATOR,
    PT_COLOR,
    PT_LINEAR_COLOR,
    PT_OTHER //
};

struct PropertyEntry {
    public:
        std::string name;           // property name
        std::string nameLower;      // lowercase for filtering (cached)
        int offset = 0;             // memory offset
        int type = PT_OTHER;        // PropertyType
        bool readonly = false;      // is it readonly?
        bool fromArray = false;     // true when this entry is an array element (readObject is array data, not a UObject)
        std::string detail;         // struct/object/array/enum name or current value label
        void* enumObj = nullptr;    // enum
        void* arrayInner = nullptr; // array inner property
        int staticArrayDim = 0;     // >0 = fixed-size array (ArrayDim>1), no FScriptArray
        std::vector<std::string> enumAvailableStrings = {};
        UStruct* structType = nullptr; // struct type
        float fVector[4] = {0, 0, 0, 0};
        float fValue = 0.0f;    // float
        int iValue = 0;         // int
        bool bValue = false;    // bool
        DWORD maskValue = 0;    // bool bitmask
        char buffer[512] = {0}; // string buffer
        bool editing = false;   // is the user editing the property?
        bool toApply = false;   // is the edit staged yet?
};

struct FScriptArray {
    public:
        void* Data;
        int32_t ArrayNum;
        int32_t ArrayMax;
};

struct StructWindow {
        std::string id;
        std::string path;
        UStruct* structType = nullptr;
        UObject* readObject = nullptr;
        UObject* writeObject = nullptr;
        int structOffset = 0;
        bool open = true;
        std::string parentId;
        ImU32 color = 0;
        ImVec2 anchorMin = ImVec2(0, 0);
        ImVec2 anchorMax = ImVec2(0, 0);
        bool hasAnchor = false;
        ImVec2 winPos = ImVec2(0, 0);
        ImVec2 winSize = ImVec2(0, 0);
};

class Properties {
    public:
        // live-edit properties
        std::vector<PropertyEntry>& properties() {
            return propertiesVector;
        }
        std::string& propertyPawn() {
            return propertyPawnString;
        }
        char* propertySearch() {
            return spawnPropertySearchBuffer;
        }
        UObject*& propertyObject() {
            return propertyObjectPointer;
        }
        int& propertyComponentIndex() {
            return propertyComponentIndexInt;
        }

        // spawn properties
        std::vector<PropertyEntry>& spawnProperties() {
            return spawnPropertiesVector;
        }
        UObject*& spawnPropertiesCDO() {
            return spawnPropertyCDOObject;
        }
        char* spawnPropertiesSearch() {
            return spawnPropertySearchBuffer;
        }

        std::string classShortName(UProperty* p);
        static void readValue(UObject* obj, PropertyEntry& e);
        void writeValue(UObject* obj, PropertyEntry& e);
        void classifyProperty(UProperty* prop, PropertyEntry& e);
        void classifyScalarProperty(UProperty* prop, PropertyEntry& e);
        bool tryExpandStructProperty(UProperty* prop, std::vector<PropertyEntry>& out, int baseOffset = 0);
        void collectProperties(UObject* obj, std::vector<PropertyEntry>& out);
        void bindSpawnProperties(const std::string& fullClass, bool force);
        void applySpawnProperties(AActor* actor, std::vector<PropertyEntry>& props);
        void renderPropertyTable(UObject* readObject, UObject* writeObject, std::vector<PropertyEntry>& props, const std::string& filter,
                                 const std::string& pathPrefix = "", const std::string& parentId = "");
        void renderPropertyEntry(UObject* readObject, UObject* writeObject, PropertyEntry& e, const std::string& id, const std::string& basePath,
                                 const std::string& parentId);
        void collectStructFields(UStruct* s, UObject* baseObj, int structOffset, std::vector<PropertyEntry>& out);
        void renderStructWindows();
        void closeStructWindows();

        bool allowArraySlackExpansion = false; // allow adding new elements to arrays (requires reallocation, can be dangerous)
    private:
        void fillEntryFromProperty(UProperty* p, PropertyEntry& e);
        std::vector<PropertyEntry> propertiesVector;
        std::string propertyPawnString;
        char propertySearchBuffer[128] = "";
        UObject* propertyObjectPointer = nullptr;
        int propertyComponentIndexInt = 0;

        std::vector<PropertyEntry> spawnPropertiesVector;
        std::string spawnPropertyClass;
        UObject* spawnPropertyCDOObject = nullptr;
        bool spawnPropertyTried = false;
        char spawnPropertySearchBuffer[128] = "";

        std::vector<StructWindow> structWindows;
        int nextChainColor = 0;

        bool isPropertyScalar(UProperty* prop);
};

static const ImU32 chainPalette[] = {
    IM_COL32(29, 43, 83, 255),    IM_COL32(126, 37, 83, 255),   IM_COL32(0, 135, 81, 255),    IM_COL32(171, 82, 54, 255),   IM_COL32(95, 87, 79, 255),
    IM_COL32(194, 195, 199, 255), IM_COL32(255, 241, 232, 255), IM_COL32(255, 0, 77, 255),    IM_COL32(255, 163, 0, 255),   IM_COL32(255, 236, 39, 255),
    IM_COL32(0, 228, 54, 255),    IM_COL32(41, 173, 255, 255),  IM_COL32(131, 118, 156, 255), IM_COL32(255, 119, 168, 255), IM_COL32(255, 204, 170, 255),
};
static const int chainPaletteCount = (int)(sizeof(chainPalette) / sizeof(chainPalette[0]));

static bool safeGetEngineArray(void* base, int offset, FScriptArray** outArr, int* outNum, int* outMax, void** outData) {
    __try {
        FScriptArray* a = (FScriptArray*)((BYTE*)base + offset);
        *outArr = a;
        *outNum = a->ArrayNum;
        *outMax = a->ArrayMax;
        *outData = a->Data;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

#endif // SAS_PROPS_H