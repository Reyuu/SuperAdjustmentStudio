#ifndef SAS_LABELS_H
#define SAS_LABELS_H

#include <functional>
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

inline std::string wrapTextToWidth(const std::string& text, float maxWidth, const std::function<float(const std::string&)>& measure) {
    if (text.empty() || maxWidth <= 0.0f) {
        return text;
    }

    std::string out;
    out.reserve(text.size() + 16);

    size_t position = 0;
    while (position <= text.size()) {
        const size_t newline = text.find('\n', position);
        const std::string line = text.substr(position, newline == std::string::npos ? std::string::npos : newline - position);

        std::string current;
        size_t wordStart = 0;
        while (wordStart <= line.size()) {
            size_t wordEnd = line.find(' ', wordStart);
            if (wordEnd == std::string::npos) {
                wordEnd = line.size();
            }
            const std::string word = line.substr(wordStart, wordEnd - wordStart);

            if (current.empty()) {
                current = word;
            } else if (measure(current + " " + word) <= maxWidth) {
                current += " " + word;
            } else {
                out += current + "\n";
                current = word;
            }

            // a single word wider than the line has to be cut
            while (measure(current) > maxWidth && current.size() > 1) {
                size_t cut = current.size() - 1;
                while (cut > 1 && measure(current.substr(0, cut)) > maxWidth) {
                    --cut;
                }
                out += current.substr(0, cut) + "\n";
                current = current.substr(cut);
            }

            if (wordEnd >= line.size()) {
                break;
            }
            wordStart = wordEnd + 1;
        }
        out += current;
        if (newline == std::string::npos) {
            break;
        }
        out.push_back('\n');
        position = newline + 1;
    }

    return out;
}

#endif // SAS_LABELS_H
