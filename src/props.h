#ifndef SAS_PROPS_H
#define SAS_PROPS_H
#include <LESDK/Includes.LE2.hpp>
#include <string>
#include <vector>
#include <sstream>
#include "util.h"
#include "logger.h"
#include "imgui.h"

#define PROP_CASE(p, t, l) if (p->IsA(t::StaticClass())) return l;

enum PropertyType : int {
    PT_FLOAT = 0, // UFloatProperty
    PT_INT, // UIntProperty
    PT_BOOL, // UBoolProperty
    PT_BYTE, // UByteProperty
    PT_ENUM, // UEnum -> complex type
    PT_NAME, // UNameProperty
    PT_STRING, // UStrProperty
    PT_OBJECT, // UClassProperty, UComponentProperty, UObjectProperty
    PT_STRUCT, // UStructProperty
    PT_ARRAY, // UArrayProperty
    PT_MASK, // UBioMask4Property
    PT_VECTOR,
    PT_VECTOR2D,
    PT_ROTATOR,
    PT_COLOR,
    PT_LINEAR_COLOR,
    PT_OTHER // 
};

struct PropertyEntry {
    std::string              name;                              // property name
    int                      offset               = 0;          // memory offset
    int                      type                 = PT_OTHER;   // PropertyType
    bool                     readonly             = false;      // is it readonly?
    std::string              detail;                            // struct/object/array/enum name or current value label
    void*                    enumObj              = nullptr;    //enum
    std::vector<std::string> enumAvailableStrings = {};
    float                    fVector[4]              = {0,0,0,0};
    float                    fValue               = 0.0f;       // float
    int                      iValue               = 0;          // int
    bool                     bValue               = false;      // bool
    DWORD                    maskValue            = 0;          // bool bitmask
    char                     buffer[512]          = {0};    // string buffer
    bool                     editing              = false;      // is the user editing the property?
    bool                     toApply              = false;      // is the edit staged yet?
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
        void readValue(UObject* obj, PropertyEntry& e);
        void writeValue(UObject* obj, PropertyEntry& e);
        void classifyScalarProperty(UProperty* prop, PropertyEntry& e);
        bool tryExpandStructProperty(UProperty* prop, std::vector<PropertyEntry>& out);
        void collectProperties(UObject* obj, std::vector<PropertyEntry>& out);
        void bindSpawnProperties(const std::string& fullClass, bool force);
        void applySpawnProperties(AActor* actor, std::vector<PropertyEntry>& props);
        void renderPropertyTable(UObject* readObject, UObject* writeObject, std::vector<PropertyEntry>& props, const std::string& filter);
    private:
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
    
        bool isPropertyScalar(UProperty* prop);
};

#endif // SAS_PROPS_H