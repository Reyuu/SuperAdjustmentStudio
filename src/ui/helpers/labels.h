#ifndef SAS_LABELS_H
#define SAS_LABELS_H

#include <string>
#include <utility>

#include "imgui.h"

inline std::pair<std::string, std::string> splitWidgetLabel(const char* label) {
    const std::string labelStr(label ? label : "");
    const size_t hash = labelStr.find("##");
    if (hash == std::string::npos) {
        return {labelStr, labelStr};
    }
    return {labelStr.substr(0, hash), "##" + labelStr.substr(hash + 2)};
}

inline void labelAbove(const char* label) {
    const std::string text = splitWidgetLabel(label).first;
    if (!text.empty()) {
        ImGui::TextWrapped("%s", text.c_str());
    }
}

inline std::string labelIdOnly(const char* label) {
    return splitWidgetLabel(label).second;
}

#endif // SAS_LABELS_H
