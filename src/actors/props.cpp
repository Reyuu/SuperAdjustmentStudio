#include "props.h"
#include "Common/FString.hpp"
#include "IconsFontAwesome6.h"
#include "LE2/Core_classes.hpp"
#include "ui/helpers/toast_notifications.h"
#include "util.h"
#include <LESDK/Common/Math.hpp>
#include <algorithm>
#include <cstring>
#include <windows.h>

static bool safeReadValue(UObject* obj, PropertyEntry& e) {
    try {
        Properties::readValue(obj, e);
        return true;
    } catch (...) {
        Logger->debug("safeReadValue: AV reading '{}' (type={}, offset={})", e.name, e.type, e.offset);
        e.detail = "(unreadable)";
        e.fValue = 0;
        e.iValue = 0;
        e.bValue = false;
        e.maskValue = 0;
        e.buffer[0] = '\0';
        return false;
    }
}

std::string Properties::classShortName(UProperty* p) {
    PROP_CASE(p, UFloatProperty, "float")
    PROP_CASE(p, UIntProperty, "int")
    PROP_CASE(p, UBoolProperty, "bool")
    PROP_CASE(p, UByteProperty, "byte")
    PROP_CASE(p, UNameProperty, "name")
    PROP_CASE(p, UStrProperty, "string")
    PROP_CASE(p, UClassProperty, "class")
    PROP_CASE(p, UComponentProperty, "component")
    PROP_CASE(p, UObjectProperty, "object")
    PROP_CASE(p, UBioMask4Property, "mask4")
    PROP_CASE(p, UStructProperty, "struct")
    PROP_CASE(p, UArrayProperty, "array")
    PROP_CASE(p, UMapProperty, "map")
    PROP_CASE(p, UDelegateProperty, "delegate")
    PROP_CASE(p, UInterfaceProperty, "interface")
    return "unknown";
}

void Properties::readValue(UObject* obj, PropertyEntry& e) {
    // read value by pointer offset, then it will be cast to whatever we need
    BYTE* p = (BYTE*)obj + e.offset;

    switch (e.type) {
        case PT_FLOAT: {
            e.fValue = *(float*)p;
            break;
        }
        case PT_INT: {
            e.iValue = *(int*)p;
            break;
        }
        case PT_BYTE: {
            e.iValue = *p;
            break;
        }
        case PT_MASK: {
            e.maskValue = *(DWORD*)p;
            break;
        }
        case PT_BOOL: {
            // bools in UE3 are packed bitmasks
            if (e.maskValue) {
                e.bValue = (*(DWORD*)p & e.maskValue) != 0;
            } else {
                e.bValue = *p != 0;
            }
            break;
        }
        case PT_ENUM: {
            UEnum* en = (UEnum*)e.enumObj;
            e.iValue = *p;
            e.detail.clear();
            // UEnum does not allow negatives
            if (en && e.iValue >= 0 && e.iValue < (int)en->Names.Count()) {
                const char* nm = en->Names.GetData()[e.iValue].GetName();
                if (nm) {
                    e.detail = nm;
                }
            }
            break;
        }
        case PT_NAME: {
            if (!e.editing) {
                const char* nm = ((SFXName*)p)->GetName();
                strncpy_s(e.buffer, sizeof(e.buffer), nm ? nm : "", _TRUNCATE);
            }
            break;
        }
        case PT_STRING: {
            if (!e.editing) {
                std::string s = FStringToUtf8(*(FString*)p);
                strncpy_s(e.buffer, sizeof(e.buffer), s.c_str(), _TRUNCATE);
            }
            break;
        }
        case PT_OBJECT: {
            UObject* obj = *(UObject**)p;
            e.detail = obj ? FStringToUtf8(obj->GetFullName()) : "(none)";
            break;
        }
        case PT_ARRAY: {
            TArray<void*>* arr = (TArray<void*>*)p;
            e.detail = "count=" + std::to_string(arr->Count());
            break;
        }
        case PT_VECTOR: {
            const float* values = (const float*)p;
            e.fVector[0] = values[0];
            e.fVector[1] = values[1];
            e.fVector[2] = values[2];
            break;
        }
        case PT_VECTOR2D: {
            const float* values = (const float*)p;
            e.fVector[0] = values[0];
            e.fVector[1] = values[1];
            break;
        }
        case PT_ROTATOR: {
            const int* values = (const int*)p;
            e.fVector[0] = UnrealRotationUnitsToDegrees(values[0]);
            e.fVector[1] = UnrealRotationUnitsToDegrees(values[1]);
            e.fVector[2] = UnrealRotationUnitsToDegrees(values[2]);
            break;
        }
        case PT_COLOR: { // it's backwards -> B,G,R,A
            const BYTE* values = (const BYTE*)p;
            e.fVector[2] = values[2] / 255.0f;
            e.fVector[1] = values[1] / 255.0f;
            e.fVector[0] = values[0] / 255.0f;
            e.fVector[3] = values[3] / 255.0f;
            break;
        }
        case PT_LINEAR_COLOR: {
            const float* values = (const float*)p;
            e.fVector[0] = values[0];
            e.fVector[1] = values[1];
            e.fVector[2] = values[2];
            e.fVector[3] = values[3];
        }
        case PT_STRUCT: {
            break;
        }
        case PT_OTHER: {
            break;
        }
        default: {
            break;
        }
    }
}

void Properties::writeValue(UObject* obj, PropertyEntry& e) {
    BYTE* p = (BYTE*)obj + e.offset;

    switch (e.type) {
        case PT_FLOAT: {
            *(float*)p = e.fValue;
            break;
        }
        case PT_INT: {
            *(int*)p = e.iValue;
            break;
        }
        case PT_BOOL: {
            if (e.maskValue) {
                if (e.bValue) {
                    // logical or
                    *(DWORD*)p |= e.maskValue;
                } else {
                    // logical nand
                    *(DWORD*)p &= ~e.maskValue;
                }
            } else {
                *p = e.bValue ? 1 : 0;
            }
            break;
        }
        case PT_BYTE: {
            case PT_ENUM: {
                *p = (BYTE)e.iValue;
                break;
            }
        }
        case PT_NAME: {
            if (e.buffer[0] == '\0') {
                break;
            }
            SFXName n(e.buffer, 0);
            *(SFXName*)p = n;
            break;
        }
        case PT_STRING: {
            FString* f = (FString*)p;
            std::wstring w = toWString(e.buffer);
            f->~FString();
            // magic
            new (f) FString(w.c_str());
            break;
        }
        case PT_MASK: {
            *(DWORD*)p = e.maskValue;
            break;
        }
        case PT_VECTOR: {
            float* values = (float*)p;
            values[0] = e.fVector[0];
            values[1] = e.fVector[1];
            values[2] = e.fVector[2];
            break;
        }
        case PT_VECTOR2D: {
            float* values = (float*)p;
            values[0] = e.fVector[0];
            values[1] = e.fVector[1];
            break;
        }
        case PT_ROTATOR: {
            int* values = (int*)p;
            values[0] = DegreesToUnrealRotationUnits(e.fVector[0]);
            values[1] = DegreesToUnrealRotationUnits(e.fVector[1]);
            values[2] = DegreesToUnrealRotationUnits(e.fVector[2]);
            break;
        }
        case PT_COLOR: {
            BYTE* values = (BYTE*)p;
            values[0] = (BYTE)std::clamp((int)(e.fVector[2] * 255.0f + 0.5f), 0, 255);
            values[1] = (BYTE)std::clamp((int)(e.fVector[1] * 255.0f + 0.5f), 0, 255);
            values[2] = (BYTE)std::clamp((int)(e.fVector[0] * 255.0f + 0.5f), 0, 255);
            values[3] = (BYTE)std::clamp((int)(e.fVector[3] * 255.0f + 0.5f), 0, 255);
            break;
        }
        case PT_LINEAR_COLOR: {
            float* values = (float*)p;
            values[0] = e.fVector[0];
            values[1] = e.fVector[1];
            values[2] = e.fVector[2];
            values[3] = e.fVector[3];
            break;
        }
        case PT_OBJECT: {
            case PT_STRUCT: {
                case PT_ARRAY: {
                    case PT_OTHER: {
                        default: {
                            break;
                        }
                    }
                }
            }
        }
    }
}

void Properties::classifyScalarProperty(UProperty* prop, PropertyEntry& e) {
    // oughhhh
    if (prop->IsA(UFloatProperty::StaticClass())) {
        e.type = PT_FLOAT;
    } else if (prop->IsA(UIntProperty::StaticClass())) {
        e.type = PT_INT;
    } else if (prop->IsA(UBoolProperty::StaticClass())) {
        e.type = PT_BOOL;
        e.maskValue = ((UBoolProperty*)prop)->BitMask;
    } else if (prop->IsA(UByteProperty::StaticClass())) {
        UByteProperty* bp = (UByteProperty*)prop;
        if (bp->Enum) {
            e.type = PT_ENUM;
            e.enumObj = bp->Enum;
            e.detail = "enum " + FStringToUtf8(bp->Enum->GetName());
            UEnum* en = (UEnum*)e.enumObj;
            for (int i = 0; i < (int)en->Names.Count(); ++i) {
                const char* nm = en->Names.GetData()[i].GetName();
                if (nm) {
                    e.enumAvailableStrings.push_back(std::string(nm));
                }
            }
        } else {
            e.type = PT_BYTE;
        }
    } else if (prop->IsA(UNameProperty::StaticClass())) {
        e.type = PT_NAME;
    } else if (prop->IsA(UStrProperty::StaticClass())) {
        e.type = PT_STRING;
    } else {
        e.type = PT_OTHER;
    }
}

bool Properties::isPropertyScalar(UProperty* prop) {
    if (prop->IsA(UFloatProperty::StaticClass())) {
        return true;
    } else if (prop->IsA(UIntProperty::StaticClass())) {
        return true;
    } else if (prop->IsA(UBoolProperty::StaticClass())) {
        return true;
    } else if (prop->IsA(UByteProperty::StaticClass())) {
        return true;
    } else if (prop->IsA(UNameProperty::StaticClass())) {
        return true;
    } else if (prop->IsA(UStrProperty::StaticClass())) {
        return true;
    }
    return false;
}

// expand small scalar-only struct properties (FColor/FVector/FRotator/FLinearColor)
bool Properties::tryExpandStructProperty(UProperty* prop, std::vector<PropertyEntry>& out, int baseOffset) {
    UStruct* s = ((UStructProperty*)prop)->Struct;
    if (!s) {
        return false;
    }

    std::vector<UProperty*> children;
    // for each extension
    for (UField* f = s->Children; f; f = f->Next) {
        if (!f->IsA(UProperty::StaticClass())) {
            continue;
        }
        UProperty* cp = (UProperty*)f;
        if (cp->Offset < 0 | cp->ArrayDim > 1) {
            return false;
        }
        bool scalar = isPropertyScalar(cp);
        if (!scalar) {
            return false;
        }
        children.push_back(cp);
    }

    if (children.empty() || children.size() > 8) {
        return false;
    }

    PropertyEntry hdr;
    hdr.name = FStringToUtf8(prop->GetName());
    hdr.offset = baseOffset + prop->Offset;
    hdr.type = PT_STRUCT;
    hdr.readonly = true;
    hdr.detail = "struct " + FStringToUtf8(s->GetName()) + " (expanded)";
    out.push_back(hdr);

    for (UProperty* cp : children) {
        PropertyEntry ce;
        ce.name = hdr.name + "." + FStringToUtf8(cp->GetName());
        ce.offset = hdr.offset + cp->Offset;
        classifyScalarProperty(cp, ce);
        out.push_back(ce);
    }
    return true;
}

void Properties::collectProperties(UObject* obj, std::vector<PropertyEntry>& out) {
    out.clear();
    if (!obj || !obj->Class) {
        return;
    }

    for (UClass* cls = obj->Class; cls; cls = (UClass*)cls->SuperField) {
        for (UField* f = cls->Children; f; f = f->Next) {
            if (!f->IsA(UProperty::StaticClass())) {
                continue;
            }

            UProperty* prop = (UProperty*)f;
            if (prop->Offset < 0) {
                continue;
            }

            PropertyEntry e;
            e.name = FStringToUtf8(prop->GetName());
            e.offset = prop->Offset;
            if (prop->ArrayDim > 1) {
                e.type = PT_ARRAY;
                e.staticArrayDim = prop->ArrayDim;
                e.arrayInner = prop;
                e.detail = "static array[" + std::to_string(prop->ArrayDim) + "] of " + classShortName(prop);
                out.push_back(e);
                continue;
            }

            classifyScalarProperty(prop, e);
            if (e.type != PT_OTHER) {
                out.push_back(e);
                continue;
            }

            if (prop->IsA(UBioMask4Property::StaticClass())) {
                e.type = PT_MASK;
            } else if (prop->IsA(UClassProperty::StaticClass()) || prop->IsA(UComponentProperty::StaticClass()) || prop->IsA(UObjectProperty::StaticClass())) {
                e.type = PT_OBJECT;
                e.readonly = true;
            } else if (prop->IsA(UStructProperty::StaticClass())) {
                UStruct* s = ((UStructProperty*)prop)->Struct;
                std::string sName = s ? FStringToUtf8(s->GetName()) : std::string();
                if (sName == "Vector") {
                    e.type = PT_VECTOR;
                } else if (sName == "Vector2D") {
                    e.type = PT_VECTOR2D;
                } else if (sName == "Rotator") {
                    e.type = PT_ROTATOR;
                } else if (sName == "Color") {
                    e.type = PT_COLOR;
                } else if (sName == "LinearColor") {
                    e.type = PT_LINEAR_COLOR;
                } else {
                    if (tryExpandStructProperty(prop, out)) {
                        continue;
                    }
                    e.type = PT_STRUCT;
                    e.readonly = true;
                    e.structType = s;
                    e.detail = s ? ("struct " + sName) : "struct ?";
                }
            } else if (prop->IsA(UArrayProperty::StaticClass())) {
                e.type = PT_ARRAY;
                e.readonly = true;
                UProperty* inner = ((UArrayProperty*)prop)->Inner;
                e.detail = "array of " + (inner ? classShortName(inner) : std::string("?")) + " (live count)";
                e.arrayInner = inner;
            } else {
                e.readonly = true;
                e.detail = classShortName(prop);
                // TODO: handle other Properties if possible (low prio)
            }
            out.push_back(e);
        }
    }
}

void Properties::bindSpawnProperties(const std::string& fullClass, bool force) {
    if (fullClass.empty()) {
        spawnPropertiesVector.clear();
        spawnPropertyClass.clear();
        spawnPropertyCDOObject = nullptr;
        closeStructWindows();
        return;
    }
    // so we don't smash FindClass every frame (expensive!)
    if (!force && spawnPropertyTried && fullClass == spawnPropertyClass) {
        return;
    }
    closeStructWindows();

    spawnPropertyTried = true;
    spawnPropertyClass = fullClass;
    spawnPropertiesVector.clear();
    spawnPropertyCDOObject = nullptr;
    std::wstring w = toWString(fullClass);
    std::wstring fullClassW = w;
    if (fullClassW.find(L"Class ") == std::wstring::npos) {
        fullClassW = L"Class " + w;
    }

    UClass* cls = UObject::FindClass(fullClassW.c_str());
    if (!cls) {
        cls = UObject::FindClass(w.c_str());
    }
    if (!cls) {
        Logger->debug("class not found: " + fullClass);
        return;
    }

    spawnPropertyCDOObject = cls->ClassDefaultObject;
    if (!spawnPropertyCDOObject) {
        Logger->debug("no class default object for " + fullClass);
        return;
    }

    collectProperties(spawnPropertyCDOObject, spawnPropertiesVector);
    std::ostringstream ss;
    ss << "bound '" << fullClass << "' (" << spawnPropertiesVector.size() << " props from CDO)";
    Logger->debug(ss.str());
}

void Properties::applySpawnProperties(AActor* actor, std::vector<PropertyEntry>& props) {
    if (!actor) {
        return;
    }

    int i = 0;
    for (PropertyEntry& e : props) {
        if (!e.toApply || e.readonly) {
            continue;
        }
        writeValue(actor, e);
        e.toApply = false;
        ++i;
    }
    std::ostringstream ss;
    ss << "applied " << i << " properties to '" << FStringToUtf8(actor->GetName()) << "'";
    Logger->debug(ss.str());
}

void Properties::renderPropertyTable(UObject* readObject, UObject* writeObject, std::vector<PropertyEntry>& props, const std::string& filter,
                                     const std::string& pathPrefix, const std::string& parentId) {
    for (int i = 0; i < (int)props.size(); i++) {
        PropertyEntry& e = props[i];
        if (!filter.empty() && toLowerStr(e.name).find(filter) == std::string::npos) {
            continue;
        }
        std::string basePath = pathPrefix.empty() ? e.name : (pathPrefix + "." + e.name);
        renderPropertyEntry(readObject, writeObject, e, std::to_string(i), basePath, parentId);
    }
}

// render a single property entry (scalar or struct)
void Properties::renderPropertyEntry(UObject* readObject, UObject* writeObject, PropertyEntry& e, const std::string& id, const std::string& basePath,
                                     const std::string& parentId) {
    std::string label = e.name + "##p" + id;
    if (!e.toApply) {
        if (!safeReadValue(readObject, e)) {
            ImGui::TextDisabled("%s: %s", e.name.c_str(), e.detail.c_str());
            return;
        }
    }

    bool changed = false;
    switch (e.type) {
        case PT_FLOAT: {
            // simple imgui widgets return bool upon change
            changed = ImGui::DragFloat(label.c_str(), &e.fValue, 0.1f);
            break;
        }
        case PT_INT: {
            changed = ImGui::DragInt(label.c_str(), &e.iValue, 1.0f);
            break;
        }
        case PT_BOOL: {
            changed = ImGui::Checkbox(label.c_str(), &e.bValue);
            break;
        }
        case PT_BYTE: {
            changed = ImGui::DragInt(label.c_str(), &e.iValue, 1.0f, 0, 255);
            break;
        }
        case PT_ENUM: {
            int maxIdx = (int)e.enumAvailableStrings.size() - 1;
            if (maxIdx < 0) {
                ImGui::TextDisabled("%s: %d (%s)", e.name.c_str(), e.iValue, e.detail.c_str());
                break;
            }
            if (e.iValue < 0) {
                e.iValue = 0;
            }
            if (e.iValue > maxIdx) {
                e.iValue = maxIdx;
            }
            changed = ImGui::SliderInt(label.c_str(), &e.iValue, 0, maxIdx, e.enumAvailableStrings[e.iValue].c_str());
            break;
        }
        case PT_NAME: {
            case PT_STRING: {
                ImGui::InputText(label.c_str(), e.buffer, sizeof(e.buffer));
                e.editing = ImGui::IsItemActive();
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    changed = true;
                }
                break;
            }
        }
        case PT_MASK: {
            // 0b00000000
            // the mask can only be 0x8 bytes in size
            changed = ImGui::InputScalar(label.c_str(), ImGuiDataType_U32, &e.maskValue, NULL, NULL, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
            break;
        }
        case PT_VECTOR: {
            changed = ImGui::DragFloat3(label.c_str(), e.fVector, 0.1f);
            break;
        }
        case PT_VECTOR2D: {
            changed = ImGui::DragFloat2(label.c_str(), e.fVector, 0.1f);
            break;
        }
        case PT_ROTATOR: {
            changed = ImGui::DragFloat3(label.c_str(), e.fVector, 0.1f, -180.0f, 180.0f, "%.1f");
            break;
        }
        case PT_COLOR: {
            case PT_LINEAR_COLOR: {
                changed = ImGui::ColorEdit4(label.c_str(), e.fVector, ImGuiColorEditFlags_AlphaBar);
                break;
            }
        }
        case PT_ARRAY: {
            std::string arrLabel = e.name + " (array of " + e.detail + ")";
            if (!e.arrayInner) {
                ImGui::TextDisabled("%s", arrLabel.c_str());
                break;
            }

            FScriptArray* arr = nullptr;
            int arrNum = 0;
            int arrMax = 0;
            void* arrData = nullptr;
            if (e.staticArrayDim > 0) {
                arrData = (BYTE*)readObject + e.offset;
                arrNum = arrMax = e.staticArrayDim;
            } else if (!safeGetEngineArray(readObject, e.offset, &arr, &arrNum, &arrMax, &arrData)) {
                ImGui::TextDisabled("%s: %s (unreadable array)", e.name.c_str(), e.detail.c_str());
                break;
            }
            UProperty* arrayInner = reinterpret_cast<UProperty*>(e.arrayInner);
            int stride = arrayInner ? arrayInner->ElementSize : 0;

            std::string treeLabel = std::string(e.name) + " (array of " + classShortName(arrayInner) + ") [" + std::to_string(arrNum) + "]";
            if (ImGui::TreeNode((e.name + "##" + id).c_str(), "%s", treeLabel.c_str())) {
                try {
                    if (stride <= 0) {
                        ImGui::TextDisabled("%s: %s (invalid array stride)", e.name.c_str(), e.detail.c_str());
                    } else if (!arrData && arrNum > 0) {
                        ImGui::TextDisabled("%s: empty array data", e.name.c_str());
                    } else if (e.staticArrayDim == 0 && (arrNum < 0 || arrNum > 5000 || arrMax < 0 || arrMax > 5000)) {
                        ImGui::TextDisabled("%s: invalid array size (%d/%d)", e.name.c_str(), arrNum, arrMax);
                    } else {
                        for (int idx = 0; idx < arrNum; ++idx) {
                            ImGui::PushID(idx);
                            try {
                                PropertyEntry innerEntry;
                                innerEntry.name = e.name + "[" + std::to_string(idx) + "]";
                                classifyScalarProperty(arrayInner, innerEntry);
                                innerEntry.fromArray = true;
                                if (innerEntry.type == PT_OTHER && arrayInner->IsA(UStructProperty::StaticClass())) {
                                    UStruct* s = ((UStructProperty*)arrayInner)->Struct;
                                    innerEntry.type = PT_STRUCT;
                                    innerEntry.readonly = true;
                                    innerEntry.structType = s;
                                    innerEntry.detail = s ? ("struct " + FStringToUtf8(s->GetName())) : "struct ?";
                                }
                                innerEntry.offset = idx * stride;
                                renderPropertyEntry((UObject*)arrData, (UObject*)arrData, innerEntry, id + "." + std::to_string(idx),
                                                    basePath + "[" + std::to_string(idx) + "]", parentId);
                                if (writeObject && arr) {
                                    ImGui::SameLine();
                                    if (ImGui::Button(ICON_FA_X)) {
                                        BYTE* base = (BYTE*)arr->Data;
                                        if (idx < arr->ArrayNum - 1) {
                                            memmove(base + idx * stride, base + (idx + 1) * stride, (arr->ArrayNum - idx - 1) * stride);
                                        }
                                        arr->ArrayNum--;
                                        ImGui::PopID();
                                        break;
                                    }
                                }
                            } catch (...) {
                                ImGui::TextDisabled("(error reading element %d)", idx);
                            }
                            ImGui::PopID();
                        }

                        if (writeObject && arr) {
                            ImGui::PushID((std::string("add") + id).c_str());
                            if (arrNum == 0) {
                                ImGui::TextDisabled("(array empty)");
                            }
                            if (arr->ArrayMax > arr->ArrayNum) {
                                if (ImGui::Button(ICON_FA_PLUS)) {
                                    BYTE* base = (BYTE*)arr->Data;
                                    memset(base + arr->ArrayNum * stride, 0, stride);
                                    arr->ArrayNum++;
                                }
                            } else if (allowArraySlackExpansion) {
                                if (ImGui::Button(ICON_FA_PLUS)) {
                                    int newMax = arr->ArrayMax > 0 ? arr->ArrayMax * 2 : 4;
                                    BYTE* newData = new BYTE[(size_t)newMax * stride];
                                    if (arr->Data) {
                                        memcpy(newData, arr->Data, (size_t)arr->ArrayNum * stride);
                                    }
                                    memset(newData + (size_t)arr->ArrayNum * stride, 0, (size_t)(newMax - arr->ArrayNum) * stride);
                                    arr->Data = newData;
                                    arr->ArrayMax = newMax;
                                    arr->ArrayNum++;
                                }
                            } else if (arrNum > 0) {
                                ImGui::TextDisabled("(array full)");
                            }
                            ImGui::PopID();
                        }
                    }
                } catch (...) {
                    ImGui::TextDisabled("(error reading array elements)");
                }
                ImGui::TreePop();
            }
            break;
        }
        case PT_OBJECT: {
            case PT_OTHER: {
                default: {
                    ImGui::TextDisabled("%s: %s", e.name.c_str(), e.detail.c_str());
                    break;
                }
            } break;
        }
        case PT_STRUCT: {
            if (!e.structType) {
                ImGui::TextDisabled("%s: %s", e.name.c_str(), e.detail.c_str());
                break;
            }
            std::string linkId = e.name + "##" + std::to_string(e.offset);
            std::string wid = "StructWin##" + basePath;
            if (ImGui::TextLink(linkId.c_str())) {
                bool alreadyOpen = false;
                for (const StructWindow& w : structWindows) {
                    if (w.id == wid) {
                        alreadyOpen = true;
                        break;
                    }
                }
                if (!alreadyOpen) {
                    StructWindow sw;
                    sw.id = wid;
                    sw.path = basePath;
                    sw.structType = e.structType;
                    sw.readObject = readObject;
                    sw.writeObject = writeObject;
                    sw.structOffset = e.offset;
                    sw.parentId = parentId;
                    if (parentId.empty()) {
                        std::string objName;
                        if (readObject && !e.fromArray) {
                            objName = FStringToUtf8(readObject->GetName());
                        }
                        sw.path = objName.empty() ? basePath : (objName + "." + basePath);
                    } else {
                        sw.path = basePath;
                    }
                    if (parentId.empty()) {
                        sw.color = chainPalette[nextChainColor % chainPaletteCount];
                        nextChainColor++;
                    } else {
                        for (const StructWindow& p : structWindows) {
                            if (p.id == parentId) {
                                sw.color = p.color;
                                break;
                            }
                        }
                    }
                    sw.anchorMin = ImGui::GetItemRectMin();
                    sw.anchorMax = ImGui::GetItemRectMax();
                    sw.hasAnchor = true;
                    structWindows.push_back(sw);
                } else {
                }
            }
            ImVec2 aMin = ImGui::GetItemRectMin();
            ImVec2 aMax = ImGui::GetItemRectMax();
            for (StructWindow& w : structWindows) {
                if (w.id == wid) {
                    w.anchorMin = aMin;
                    w.anchorMax = aMax;
                    w.hasAnchor = true;
                    break;
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s) - click to edit", e.detail.c_str());
            break;
        }
    }
    if (changed) {
        if (writeObject) {
            writeValue(writeObject, e);
            if (e.type == PT_ENUM) {
                safeReadValue(readObject, e); // enum value refresh
            }
        } else {
            e.toApply = true;
        }
    }
}

void Properties::fillEntryFromProperty(UProperty* p, PropertyEntry& e) {
    classifyScalarProperty(p, e);

    if (e.type != PT_OTHER) {
        return;
    }
    if (p->IsA(UBioMask4Property::StaticClass())) {
        e.type = PT_MASK;
    } else if (p->IsA(UClassProperty::StaticClass()) || p->IsA(UComponentProperty::StaticClass()) || p->IsA(UObjectProperty::StaticClass())) {
        e.type = PT_OBJECT;
        e.readonly = true;
    } else if (p->IsA(UStructProperty::StaticClass())) {
        UStruct* s = ((UStructProperty*)p)->Struct;
        std::string sName = s ? FStringToUtf8(s->GetName()) : std::string();
        if (sName == "Vector") {
            e.type = PT_VECTOR;
        } else if (sName == "Vector2D") {
            e.type = PT_VECTOR2D;
        } else if (sName == "Rotator") {
            e.type = PT_ROTATOR;
        } else if (sName == "Color") {
            e.type = PT_COLOR;
        } else if (sName == "LinearColor") {
            e.type = PT_LINEAR_COLOR;
        } else {
            e.type = PT_STRUCT;
            e.readonly = true;
            e.structType = s;
            e.detail = s ? ("struct " + sName) : "struct ?";
        }
    } else if (p->IsA(UArrayProperty::StaticClass())) {
        e.type = PT_ARRAY;
        e.readonly = true;
        UProperty* inner = ((UArrayProperty*)p)->Inner;
        e.detail = "array of " + (inner ? classShortName(inner) : std::string("?")) + " (live count)";
        e.arrayInner = inner;
    } else {
        e.readonly = true;
        e.detail = classShortName(p);
    }
}

void Properties::collectStructFields(UStruct* s, UObject* baseObj, int structOffset, std::vector<PropertyEntry>& out) {
    out.clear();
    if (!s) {
        return;
    }

    for (UClass* cls = (UClass*)s; cls; cls = (UClass*)cls->SuperField) {
        for (UField* f = cls->Children; f; f = f->Next) {
            if (!f->IsA(UProperty::StaticClass())) {
                continue;
            }

            UProperty* prop = (UProperty*)f;
            if (prop->Offset < 0) {
                continue;
            }

            PropertyEntry e;
            e.name = FStringToUtf8(prop->GetName());
            e.offset = structOffset + prop->Offset;

            if (prop->ArrayDim > 1) {
                e.type = PT_ARRAY;
                e.staticArrayDim = prop->ArrayDim;
                e.arrayInner = prop;
                e.detail = "static array[" + std::to_string(prop->ArrayDim) + "] of " + classShortName(prop);
                out.push_back(e);
                continue; // skip static arrays
            }

            classifyScalarProperty(prop, e);
            if (e.type != PT_OTHER) {
                out.push_back(e);
                continue;
            }

            if (prop->IsA(UStructProperty::StaticClass())) {
                UStruct* childStruct = ((UStructProperty*)prop)->Struct;
                std::string sName = childStruct ? FStringToUtf8(childStruct->GetName()) : std::string();
                if (sName == "Vector" || sName == "Vector2D" || sName == "Rotator" || sName == "Color" || sName == "LinearColor") {
                    fillEntryFromProperty(prop, e);
                    out.push_back(e);
                    continue;
                }
                if (tryExpandStructProperty(prop, out, structOffset)) {
                    continue;
                }
            }

            fillEntryFromProperty(prop, e);
            out.push_back(e);
        }
    }
}

void Properties::renderStructWindows() {
    ImGuiIO& io = ImGui::GetIO();
    int windowCount = (int)structWindows.size();
    for (int i = 0; i < windowCount; i++) {
        bool open = structWindows[i].open;
        std::string id = structWindows[i].id;
        ImU32 color = structWindows[i].color;
        std::string path = structWindows[i].path;
        UStruct* structType = structWindows[i].structType;
        UObject* readObject = structWindows[i].readObject;
        UObject* writeObject = structWindows[i].writeObject;
        int offset = structWindows[i].structOffset;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(650, 450), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(650, 450), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleColor(ImGuiCol_Border, color);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

        bool began = ImGui::Begin(id.c_str(), &open);
        if (began) {
            try {
                structWindows[i].winPos = ImGui::GetWindowPos();
                structWindows[i].winSize = ImGui::GetWindowSize();
                ImGui::Text("Struct path: %s", path.c_str());
                if (structType) {
                    ImGui::TextDisabled("type %s", FStringToUtf8(structType->GetName()).c_str());
                }
                ImGui::Separator();

                if (!structType || !readObject) {
                    ImGui::TextDisabled("invalid struct or object (has the target changed?)");
                } else {
                    std::vector<PropertyEntry> structProps;
                    collectStructFields(structType, readObject, offset, structProps);
                    renderPropertyTable(readObject, writeObject, structProps, "", path, id);
                }
            } catch (...) {
                ImGui::TextDisabled("(error reading struct fields)");
            }
            ImGui::End();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        structWindows[i].open = open;
    }

    bool cascade;
    do {
        cascade = false;
        for (int i = 0; i < (int)structWindows.size(); i++) {
            if (!structWindows[i].open) {
                std::vector<std::string> dead{structWindows[i].id};
                bool grew = true;
                while (grew) {
                    grew = false;
                    for (const StructWindow& w : structWindows) {
                        if (std::find(dead.begin(), dead.end(), w.parentId) != dead.end() && std::find(dead.begin(), dead.end(), w.id) == dead.end()) {
                            dead.push_back(w.id);
                            grew = true;
                        }
                    }
                }
                structWindows.erase(std::remove_if(structWindows.begin(), structWindows.end(),
                                                   [&](const StructWindow& w) {
                                                       return std::find(dead.begin(), dead.end(), w.id) != dead.end();
                                                   }),
                                    structWindows.end());
                cascade = true;
                break;
            }
        }
    } while (cascade);

    // draw arrows from parent to child windows
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    for (const StructWindow& w : structWindows) {
        if (!w.hasAnchor || w.parentId.empty()) {
            continue;
        }
        ImVec2 start, end;
        const StructWindow* parent = nullptr;
        for (const StructWindow& p : structWindows) {
            if (p.id == w.parentId) {
                parent = &p;
                break;
            }
        }
        if (parent) {
            ImVec2 const pPos = parent->winPos;
            ImVec2 const pSize = parent->winSize;
            ImVec2 const cPos = w.winPos;
            ImVec2 const cSize = w.winSize;
            float const dx = (cPos.x + cSize.x * 0.5f) - (pPos.x + pSize.x * 0.5f);
            float const dy = (cPos.y + cSize.y * 0.5f) - (pPos.y + pSize.y * 0.5f);
            if (fabsf(dy) >= fabsf(dx)) {
                if (dy >= 0.0f) {
                    start = ImVec2(pPos.x + pSize.x * 0.5f, pPos.y + pSize.y);
                    end = ImVec2(cPos.x + cSize.x * 0.5f, cPos.y);
                } else {
                    start = ImVec2(pPos.x + pSize.x * 0.5f, pPos.y);
                    end = ImVec2(cPos.x + cSize.x * 0.5f, cPos.y + cSize.y);
                }
            } else {
                if (dx >= 0.0f) {
                    start = ImVec2(pPos.x + pSize.x, pPos.y + pSize.y * 0.5f);
                    end = ImVec2(cPos.x, cPos.y + cSize.y * 0.5f);
                } else {
                    start = ImVec2(pPos.x, pPos.y + pSize.y * 0.5f);
                    end = ImVec2(cPos.x + cSize.x, cPos.y + cSize.y * 0.5f);
                }
            }
        } else {
            start = ImVec2(w.anchorMax.x, (w.anchorMax.y + w.anchorMin.y) * 0.5f);
            end = ImVec2(w.winPos.x, w.winPos.y + w.winSize.y * 0.5f);
        }
        ImVec2 arrowDir = ImVec2(end.x - start.x, end.y - start.y);
        float len = sqrtf(arrowDir.x * arrowDir.x + arrowDir.y * arrowDir.y);
        ImVec2 bh1, bh2, arrowHead1, arrowHead2;
        bool hasHead = false;
        if (len > 1.0f) {
            arrowDir.x /= len;
            arrowDir.y /= len;
            arrowHead1 = ImVec2(end.x - arrowDir.x * 10.0f + arrowDir.y * 5.0f, end.y - arrowDir.y * 10.0f - arrowDir.x * 5.0f);
            arrowHead2 = ImVec2(end.x - arrowDir.x * 10.0f - arrowDir.y * 5.0f, end.y - arrowDir.y * 10.0f + arrowDir.x * 5.0f);
            bh1 = ImVec2(end.x - arrowDir.x * 12.0f + arrowDir.y * 7.0f, end.y - arrowDir.y * 12.0f - arrowDir.x * 7.0f);
            bh2 = ImVec2(end.x - arrowDir.x * 12.0f - arrowDir.y * 7.0f, end.y - arrowDir.y * 12.0f + arrowDir.x * 7.0f);
            hasHead = true;
        }
        if (hasHead) {
            drawList->AddTriangleFilled(end, bh1, bh2, IM_COL32(0, 0, 0, 255));
        }
        drawList->AddLine(start, end, IM_COL32(0, 0, 0, 255), 4.0f);
        drawList->AddLine(start, end, w.color, 2.0f);
        if (hasHead) {
            drawList->AddTriangleFilled(end, arrowHead1, arrowHead2, w.color);
        }
    }
}

void Properties::closeStructWindows() {
    structWindows.clear();
}
