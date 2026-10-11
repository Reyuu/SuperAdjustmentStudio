#include "application.h"
#include "custom_shader_catalog.h"
#include "custom_shader_effect.h"
#include "logger.h"
#include "settings.h"
#include "translation.h"
#include "ui/helpers/labels.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <shellapi.h>

namespace {

std::string displayPath(const std::filesystem::path& path) {
    char buffer[1024] = {};
    snprintf(buffer, sizeof(buffer), "%s", path.string().c_str());
    return std::string(buffer);
}

void openFolder(const std::filesystem::path& path) {
    const std::string value = path.string();
    ShellExecuteA(nullptr, "open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

float& floatValue(CustomShaderOptions& options, const CustomShaderParam& param) {
    return options.floats[param.name];
}

int& intValue(CustomShaderOptions& options, const CustomShaderParam& param) {
    return options.ints[param.name];
}

void resetValues(CustomShaderOptions& options, const std::vector<CustomShaderParam>& params) {
    for (const auto& param : params) {
        switch (param.kind) {
            case CustomShaderParamKind::Color: {
                std::array<float, 3> color = {param.colorDef[0], param.colorDef[1], param.colorDef[2]};
                options.colors[param.name] = color;
                break;
            }
            case CustomShaderParamKind::DragInt: {
                options.ints[param.name] = param.intDef;
                break;
            }
            case CustomShaderParamKind::InputInt: {
                options.ints[param.name] = param.intDef;
                break;
            }
            default: {
                options.floats[param.name] = param.def;
                break;
            }
        }
    }
}

bool renderParam(CustomShaderOptions& options, const CustomShaderParam& param, const char* id) {
    ImGui::TextDisabled("%s", param.name.c_str());
    const std::string label = std::string("##cs_") + id;
    bool changed = false;

    switch (param.kind) {
        case CustomShaderParamKind::Slider: {
            float& value = floatValue(options, param);
            changed = ImGui::SliderFloat(label.c_str(), &value, param.min, param.max, "%.3f");
            break;
        }
        case CustomShaderParamKind::Drag: {
            float& value = floatValue(options, param);
            changed = ImGui::DragFloat(label.c_str(), &value, param.step > 0.0f ? param.step : 0.01f, param.min, param.max, "%.3f");
            break;
        }
        case CustomShaderParamKind::Input: {
            float& value = floatValue(options, param);
            changed = ImGui::InputFloat(label.c_str(), &value, param.step > 0.0f ? param.step : 0.0f);
            break;
        }
        case CustomShaderParamKind::DragInt: {
            int& value = intValue(options, param);
            changed = ImGui::DragInt(label.c_str(), &value, param.step > 0.0f ? param.step : 1.0f, (int)param.min, (int)param.max);
            break;
        }
        case CustomShaderParamKind::InputInt: {
            int& value = intValue(options, param);
            changed = ImGui::InputInt(label.c_str(), &value, param.step > 0.0f ? (int)param.step : 1, (int)param.min, (int)param.max);
            break;
        }
        case CustomShaderParamKind::Toggle: {
            float& value = floatValue(options, param);
            bool enabled = value != 0.0f;
            if (ImGui::Checkbox(label.c_str(), &enabled)) {
                value = enabled ? 1.0f : 0.0f;
                changed = true;
            }
            break;
        }
        case CustomShaderParamKind::Radio: {
            float& value = floatValue(options, param);
            int selected = std::clamp((int)value, 0, (int)param.items.size() - 1);
            for (size_t i = 0; i < param.items.size(); ++i) {
                const std::string itemLabel = param.items[i] + "##cs_radio_" + id + "_" + std::to_string(i);
                if (ImGui::RadioButton(itemLabel.c_str(), selected == (int)i)) {
                    selected = (int)i;
                    changed = true;
                }
                if (i + 1 < param.items.size()) {
                    ImGui::SameLine();
                }
            }
            if (changed) {
                value = (float)selected;
            }
            break;
        }
        case CustomShaderParamKind::Combo: {
            float& value = floatValue(options, param);
            int selected = std::clamp((int)value, 0, (int)param.items.size() - 1);
            if (ImGui::BeginCombo(label.c_str(), param.items[selected].c_str())) {
                for (size_t i = 0; i < param.items.size(); ++i) {
                    const std::string itemLabel = param.items[i] + "##cs_combo_" + id + "_" + std::to_string(i);
                    if (ImGui::Selectable(itemLabel.c_str(), selected == (int)i)) {
                        selected = (int)i;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndCombo();
            }
            if (changed) {
                value = (float)selected;
            }
            break;
        }
        case CustomShaderParamKind::ListBox: {
            float& value = floatValue(options, param);
            int selected = std::clamp((int)value, 0, (int)param.items.size() - 1);
            if (ImGui::BeginListBox(label.c_str(), ImVec2(0, (float)param.items.size() * ImGui::GetTextLineHeightWithSpacing()))) {
                for (size_t i = 0; i < param.items.size(); ++i) {
                    const std::string itemLabel = param.items[i] + "##cs_list_" + id + "_" + std::to_string(i);
                    if (ImGui::Selectable(itemLabel.c_str(), selected == (int)i)) {
                        selected = (int)i;
                        changed = true;
                    }
                }
                ImGui::EndListBox();
            }
            if (changed) {
                value = (float)selected;
            }
            break;
        }
        case CustomShaderParamKind::Color: {
            std::array<float, 3>& stored = options.colors[param.name];
            if (param.hasAlpha) {
                float rgba[4] = {stored[0], stored[1], stored[2], param.colorDef[3]};
                if (ImGui::ColorEdit4(label.c_str(), rgba)) {
                    stored = {rgba[0], rgba[1], rgba[2]};
                    changed = true;
                }
            } else {
                float rgb[3] = {stored[0], stored[1], stored[2]};
                if (ImGui::ColorEdit3(label.c_str(), rgb)) {
                    stored = {rgb[0], rgb[1], rgb[2]};
                    changed = true;
                }
            }
            break;
        }
    }

    return changed;
}

// wraps at the panel edge, keeps embedded newlines and breaks the long paths
// fxc prints in front of each message. the breaks are computed here: relying on
// ImGui's wrap state inside a child window does not reliably wrap.
void renderError(const std::string& error, const std::string& id) {
    static constexpr float ERROR_HEIGHT = 160.0f;

    ImGui::PushID(id.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
    if (ImGui::BeginChild("##cs_error", ImVec2(ImGui::GetContentRegionAvail().x, ERROR_HEIGHT), false)) {
        const std::string wrapped = wrapTextToWidth(error, ImGui::GetContentRegionAvail().x, [](const std::string& text) {
            return ImGui::CalcTextSize(text.c_str()).x;
        });
        ImGui::TextUnformatted(wrapped.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

} // namespace

void CustomShaderEffect::renderUI() {
    ImGui::TextUnformatted(t("ui.custom_shader.title"));

    const auto& roots = customShaderRoots();
    for (size_t i = 0; i < roots.size(); ++i) {
        ImGui::TextDisabled("%s", displayPath(roots[i]).c_str());
    }

    Settings& settings = Application::instance().settings();
    auto& options = settings.options.customShaders;
    ID3D11Device* device = Application::instance().renderer().device();

    if (ImGui::SmallButton(ICON_FA_ARROW_ROTATE_RIGHT "##cs_rescan")) {
        rescan();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ROTATE "##cs_recompile")) {
        recompileAll(device);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN "##cs_open_folder")) {
        openFolder(roots.empty() ? std::filesystem::current_path() : roots[0]);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_PLUS "##cs_add")) {
        ImGui::OpenPopup("##cs_add_popup");
    }

    if (ImGui::BeginPopup("##cs_add_popup")) {
        const auto entries = catalog();
        ImGui::TextDisabled(t("ui.custom_shader.add_hint"), (int)options.size(), CUSTOM_SHADER_MAX_SLOTS);
        ImGui::Separator();
        if ((int)options.size() >= CUSTOM_SHADER_MAX_SLOTS) {
            ImGui::TextDisabled(t("ui.custom_shader.chain_is_full"));
        } else if (entries.empty()) {
            ImGui::TextDisabled(t("ui.custom_shader.no_shaders_found"));
        } else {
            static char search[128] = {};
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##cs_search", t("ui.custom_shader.search"), search, sizeof(search));
            ImGui::PopItemWidth();

            std::string needle = search;
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });

            ImGui::BeginChild("##cs_add_list", ImVec2(320, 220));
            for (const auto& entry : entries) {
                std::string haystack = entry.displayName;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) {
                    return (char)::tolower(c);
                });
                if (!needle.empty() && haystack.find(needle) == std::string::npos) {
                    continue;
                }
                bool alreadyUsed = false;
                for (const auto& slotOptions : options) {
                    if (slotOptions.ref == entry.ref) {
                        alreadyUsed = true;
                        break;
                    }
                }
                const std::string label = entry.displayName + "##cs_add_" + entry.ref;
                if (alreadyUsed) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 0));
                    ImGui::EndDisabled();
                    continue;
                }
                if (ImGui::Selectable(label.c_str())) {
                    CustomShaderOptions slot;
                    slot.ref = entry.ref;
                    CustomShaderSlot* program = slotFor(entry.absPath);
                    if (program) {
                        resetValues(slot, program->params());
                    }
                    options.push_back(slot);
                    settings.markChanged();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
        }
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_FILE_IMPORT "##cs_add_file")) {
            std::string picked;
            const std::string fileFilter = std::string(t("ui.custom_shader.file_filter_name")) + "|*.hlsl";
            if (pickFile(picked, fileFilter.c_str())) {
                std::filesystem::path absPath(picked);
                const std::string ref = makeCustomShaderRef(absPath);
                bool alreadyUsed = false;
                for (const auto& slotOptions : options) {
                    if (slotOptions.ref == ref) {
                        alreadyUsed = true;
                        break;
                    }
                }
                if (!alreadyUsed && (int)options.size() < CUSTOM_SHADER_MAX_SLOTS) {
                    CustomShaderOptions slot;
                    slot.ref = ref;
                    CustomShaderSlot* program = ensureSlot(device, absPath);
                    if (program) {
                        resetValues(slot, program->params());
                    }
                    options.push_back(slot);
                    settings.markChanged();
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (options.empty()) {
        ImGui::TextDisabled(t("ui.custom_shader.no_slots"));
    }

    static int selectedSlot = -1;
    if (selectedSlot >= (int)options.size()) {
        selectedSlot = (int)options.size() - 1;
    }

    ImGui::BeginChild("##cs_slots", ImVec2(0, 280), true);
    int removeAt = -1;
    int dropAt = -1;
    int dropFrom = -1;
    if (ImGui::BeginTable("##cs_slot_table", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##cs_slot_name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##cs_slot_buttons", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        for (size_t i = 0; i < options.size(); ++i) {
            CustomShaderOptions& slotOptions = options[i];
            std::filesystem::path absPath;
            if (!resolveCustomShaderRef(slotOptions.ref, absPath)) {
                absPath = std::filesystem::path(slotOptions.ref);
            }
            CustomShaderSlot* program = slotFor(absPath);
            const std::string error = program ? program->lastError() : errorFor(absPath);

            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool enabled = slotOptions.enabled;
            if (ImGui::Checkbox("##cs_enabled", &enabled)) {
                slotOptions.enabled = enabled;
                settings.markChanged();
            }
            ImGui::SameLine();

            const ImVec2 gripSize = ImGui::CalcTextSize(ICON_FA_GRIP_VERTICAL);
            ImGui::InvisibleButton("##cs_drag", ImVec2(gripSize.x + 8.0f, ImGui::GetFrameHeight()));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                int sourceIndex = (int)i;
                ImGui::SetDragDropPayload("CUSTOM_SHADER_SLOT", &sourceIndex, sizeof(sourceIndex));
                ImGui::TextUnformatted(slotOptions.ref.c_str());
                ImGui::EndDragDropSource();
            }
            {
                const ImVec2 rectMin = ImGui::GetItemRectMin();
                const ImVec2 rectMax = ImGui::GetItemRectMax();
                const ImVec2 textPos = ImVec2(rectMin.x + (rectMax.x - rectMin.x - gripSize.x) * 0.5f, rectMin.y + (rectMax.y - rectMin.y - gripSize.y) * 0.5f);
                ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_FA_GRIP_VERTICAL);
            }
            ImGui::SameLine();

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedSlot == (int)i) {
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            }
            const bool open = ImGui::TreeNodeEx("##cs_node", nodeFlags, "%s", slotOptions.ref.c_str());
            if (ImGui::IsItemClicked()) {
                selectedSlot = (int)i;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                int sourceIndex = (int)i;
                ImGui::SetDragDropPayload("CUSTOM_SHADER_SLOT", &sourceIndex, sizeof(sourceIndex));
                ImGui::TextUnformatted(slotOptions.ref.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CUSTOM_SHADER_SLOT")) {
                    dropFrom = *(const int*)payload->Data;
                    dropAt = (int)i;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_FA_TRASH_CAN "##cs_delete")) {
                removeAt = (int)i;
            }

            if (open) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);

                if (!error.empty()) {
                    // compiler output is long and full of newlines: wrap it inside the
                    // panel instead of forcing the overlay to be resized
                    renderError(error, absPath.filename().string());
                } else if (program && program->isReady()) {
                    ImGui::TextDisabled(t("ui.custom_shader.loaded_summary"), program->annotationFloatCount(), program->annotationIntCount(),
                                        program->annotationColorCount());
                } else {
                    ImGui::TextDisabled(t("ui.custom_shader.not_loaded"));
                }

                if (program && program->isReady()) {
                    for (const auto& param : program->params()) {
                        const std::string id = slotOptions.ref + "_" + param.name;
                        if (renderParam(slotOptions, param, id.c_str())) {
                            settings.markChanged();
                        }
                    }
                    const std::string resetLabel = std::string(ICON_FA_ARROW_RIGHT_TO_BRACKET " ") + t("ui.custom_shader.reset");
                    if (ImGui::SmallButton((resetLabel + "##cs_reset").c_str())) {
                        resetValues(slotOptions, program->params());
                        settings.markChanged();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_ROTATE_RIGHT "##cs_reload_one")) {
                        std::string reloadError;
                        program->load(device, absPath, reloadError);
                    }
                }

                ImGui::PopItemWidth();
                ImGui::Unindent();
            }

            // drop gap between rows
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::InvisibleButton("##cs_gap", ImVec2(-1.0f, 4.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CUSTOM_SHADER_SLOT")) {
                    dropFrom = *(const int*)payload->Data;
                    dropAt = (int)i + 1;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (dropAt >= 0 && dropFrom >= 0 && dropAt != dropFrom) {
        CustomShaderOptions moved = options[dropFrom];
        options.erase(options.begin() + dropFrom);
        int at = dropAt;
        if (at > (int)options.size()) {
            at = (int)options.size();
        }
        options.insert(options.begin() + at, moved);
        settings.markChanged();
        selectedSlot = at;
    }
    if (removeAt >= 0) {
        options.erase(options.begin() + removeAt);
        settings.markChanged();
        if (selectedSlot >= (int)options.size()) {
            selectedSlot = (int)options.size() - 1;
        }
    }
}