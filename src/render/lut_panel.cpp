#include "lut_effect.h"
#include "translation.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "IconsFontAwesome6.h"
#include "application.h"
#include "logger.h"
#include "settings.h"

void LutEffect::renderUI() {
    ImGui::TextUnformatted(t("ui.lut_panel.title"));
    Settings& settings = Application::instance().settings();
    auto& layers = settings.options.lutLayers;

    ImGui::TextDisabled(t("ui.lut_panel.baked_shots_hint"));
    {
        char dirBuf[1024] = {};
        snprintf(dirBuf, sizeof(dirBuf), "%s", sasLutDirectory().string().c_str());
        ImGui::TextDisabled("%s", dirBuf);
    }

    if (ImGui::SmallButton(ICON_FA_ARROW_ROTATE_RIGHT "##lut_rescan")) {
        rescan();
    }
    ImGui::Separator();
    ImGui::TextUnformatted(t("ui.lut_panel.depth_inspector"));
    bool capture = settings.options.lutDepthCapture;
    if (ImGui::Checkbox(t("ui.lut_panel.enable_depth_capture"), &capture)) {
        settings.options.lutDepthCapture = capture;
        settings.markChanged();
    }
    if (settings.options.lutDepthCapture) {
        {
            static std::vector<DepthSourceInfo> depths;
            static bool depthsInit = false;
            if (!depthsInit) {
                depthsInit = true;
                depths = depth().sourceList();
            }
            std::string current = t("ui.lut_panel.depth_auto");
            if (!settings.options.lutDepthSource.empty()) {
                current = settings.options.lutDepthSource;
            }
            ImGui::PushItemWidth(-100);
            ImGui::Text(t("ui.lut_panel.depth_source"));
            if (ImGui::BeginCombo("##lut_depth_source", current.c_str())) {
                const bool isAuto = settings.options.lutDepthSource.empty();
                if (ImGui::Selectable(t("ui.lut_panel.depth_auto"), isAuto)) {
                    settings.options.lutDepthSource.clear();
                    settings.markChanged();
                }
                for (const auto& d : depths) {
                    const bool sel = (settings.options.lutDepthSource == d.key);
                    std::string itemLabel = d.label + "##" + d.key;
                    if (ImGui::Selectable(itemLabel.c_str(), sel)) {
                        settings.options.lutDepthSource = d.key;
                        settings.markChanged();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ARROW_ROTATE_RIGHT "##lut_depth_rescan")) {
                depths = depth().sourceList();
            }
            bool freeze = freezeDepth();
            if (ImGui::Checkbox(t("ui.lut_panel.freeze"), &freeze)) {
                freezeDepth() = freeze;
            }
            ImGui::SameLine();
            bool lin = settings.options.lutDepthLinearize;
            if (ImGui::Checkbox(t("ui.lut_panel.linearize"), &lin)) {
                settings.options.lutDepthLinearize = lin;
                settings.markChanged();
            }
            ImGui::SameLine();
            bool inv = settings.options.lutDepthInvert;
            if (ImGui::Checkbox(t("ui.lut_panel.invert"), &inv)) {
                settings.options.lutDepthInvert = inv;
                settings.markChanged();
            }
            ImGui::Text(t("ui.lut_panel.near"));
            if (ImGui::SliderFloat("##depth_inspect_near", &settings.options.lutDepthNear, 0.01f, 10000.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
                settings.markChanged();
            }
            ImGui::Text(t("ui.lut_panel.far"));
            if (ImGui::SliderFloat("##depth_inspect_far", &settings.options.lutDepthFar, 0.1f, 1000000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                settings.markChanged();
            }
            ImGui::Text(t("ui.lut_panel.update_every_n"));
            if (ImGui::SliderInt("##depth_inspect_every_n", &settings.options.lutDepthEveryN, 1, 30)) {
                settings.markChanged();
            }
            bool showTex = settings.options.lutDepthShowTexture;
            ImGui::Text(t("ui.lut_panel.show_depth_texture"));
            if (ImGui::Checkbox("##depth_inspect_show_tex", &showTex)) {
                settings.options.lutDepthShowTexture = showTex;
                settings.markChanged();
            }
            ImGui::PopItemWidth();
        }
    }
    ImGui::Separator();

    const auto& catalog = this->catalog();
    if (catalog.empty()) {
        ImGui::TextDisabled(t("ui.lut_panel.no_luts_found"));
    }

    static int selectedLayer = -1;
    if (selectedLayer >= (int)layers.size()) {
        selectedLayer = (int)layers.size() - 1;
    }

    ImGui::BeginChild("##lut_layers", ImVec2(0, 280), true);
    int removeAt = -1;
    int dropAt = -1;
    int dropFrom = -1;
    if (ImGui::BeginTable("##lut_layer_table", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##lut_layer_name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##lut_layer_btns", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        for (size_t i = 0; i < layers.size(); ++i) {
            LutLayerOptions& l = layers[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool sel = ((int)i == selectedLayer);
            bool enabled = l.enabled;
            if (ImGui::Checkbox("##lut_en", &enabled)) {
                l.enabled = enabled;
                settings.markChanged();
            }
            ImGui::SameLine();
            // grab handle
            ImVec2 gripSize = ImGui::CalcTextSize(ICON_FA_GRIP_VERTICAL);
            ImGui::InvisibleButton("##lut_drag", ImVec2(gripSize.x + 8.0f, ImGui::GetFrameHeight()));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                int srcIdx = (int)i;
                ImGui::SetDragDropPayload("LUT_LAYER", &srcIdx, sizeof(srcIdx));
                ImGui::TextUnformatted(l.ref.c_str());
                ImGui::EndDragDropSource();
            }
            {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImVec2 tpos = ImVec2(rmin.x + (rmax.x - rmin.x - gripSize.x) * 0.5f, rmin.y + (rmax.y - rmin.y - gripSize.y) * 0.5f);
                ImGui::GetWindowDrawList()->AddText(tpos, ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_FA_GRIP_VERTICAL);
            }
            ImGui::SameLine();
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (sel) {
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            }
            const bool open = ImGui::TreeNodeEx("##lut_node", nodeFlags, "%s", l.ref.c_str());
            if (ImGui::IsItemClicked()) {
                selectedLayer = (int)i;
            }
            // drag from the whole row, the grip icon decoration only
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                int srcIdx = (int)i;
                ImGui::SetDragDropPayload("LUT_LAYER", &srcIdx, sizeof(srcIdx));
                ImGui::TextUnformatted(l.ref.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LUT_LAYER")) {
                    dropFrom = *(const int*)payload->Data;
                    dropAt = (int)i;
                    Logger->debug("lut: drop layer {} -> row {}", dropFrom, dropAt);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_FA_TRASH_CAN "##lut_del")) {
                removeAt = (int)i;
            }
            if (open) {
                // own row in the content column (the button column starves sliders).
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
                ImGui::TextDisabled(t("ui.lut_panel.blend"));
                if (ImGui::SliderFloat("##lut_blend", &l.blend, 0.0f, 1.0f, "%.3f")) {
                    settings.markChanged();
                }
                ImGui::TextDisabled(t("ui.lut_panel.chroma"));
                if (ImGui::SliderFloat("##lut_chroma", &l.chroma, 0.0f, 2.0f, "%.3f")) {
                    settings.markChanged();
                }
                ImGui::TextDisabled(t("ui.lut_panel.luma"));
                if (ImGui::SliderFloat("##lut_luma", &l.luma, 0.0f, 2.0f, "%.3f")) {
                    settings.markChanged();
                }
                bool useDepth = l.useDepth;
                if (ImGui::Checkbox(t("ui.lut_panel.depth_gate"), &useDepth)) {
                    l.useDepth = useDepth;
                    settings.markChanged();
                }
                if (l.useDepth) {
                    ImGui::TextDisabled(t("ui.lut_panel.focus"));
                    if (ImGui::SliderFloat("##lut_focus", &l.depthFocus, 0.0f, 1.0f, "%.3f")) {
                        settings.markChanged();
                    }
                    ImGui::TextDisabled(t("ui.lut_panel.range"));
                    if (ImGui::SliderFloat("##lut_range", &l.depthRange, 0.0f, 1.0f, "%.3f")) {
                        settings.markChanged();
                    }
                    ImGui::TextDisabled(t("ui.lut_panel.blend_min"));
                    if (ImGui::SliderFloat("##lut_bmin", &l.blendMin, 0.0f, 1.0f, "%.3f")) {
                        settings.markChanged();
                    }
                    ImGui::TextDisabled(t("ui.lut_panel.blend_max"));
                    if (ImGui::SliderFloat("##lut_bmax", &l.blendMax, 0.0f, 1.0f, "%.3f")) {
                        settings.markChanged();
                    }
                    if (ImGui::Checkbox(t("ui.lut_panel.preview_depth_gate"), &l.previewDepth)) {
                        // radio: one preview at a time
                        if (l.previewDepth) {
                            for (size_t j = 0; j < layers.size(); ++j) {
                                if (j != i) {
                                    layers[j].previewDepth = false;
                                }
                            }
                        }
                        settings.markChanged();
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox(t("ui.lut_panel.heatmap"), &l.heatPreview)) {
                        settings.markChanged();
                    }
                }
                ImGui::PopItemWidth();
                ImGui::Unindent();
            }
            // inter-row drop gap
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::InvisibleButton("##lut_gap", ImVec2(-1.0f, 4.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LUT_LAYER")) {
                    dropFrom = *(const int*)payload->Data;
                    dropAt = (int)i + 1;
                    Logger->debug("lut: drop layer {} -> gap {}", dropFrom, dropAt);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (dropAt >= 0 && dropFrom >= 0 && dropAt != dropFrom) {
            LutLayerOptions tmp = layers[dropFrom];
            layers.erase(layers.begin() + dropFrom);
            int at = dropAt;
            if (at > (int)layers.size()) {
                at = (int)layers.size();
            }
            layers.insert(layers.begin() + at, tmp);
            settings.markChanged();
            selectedLayer = at;
        }
    }
    ImGui::EndChild();

    float cornerW = 60.0f;
    if (ImGui::Button(ICON_FA_PLUS "##lut_add")) {
        ImGui::OpenPopup("##lut_add_popup");
    }
    if (ImGui::BeginPopup("##lut_add_popup")) {
        ImGui::TextDisabled(t("ui.lut_panel.add_lut"), (int)layers.size(), LUT_MAX_LAYERS);
        ImGui::Separator();
        if (layers.size() >= (size_t)LUT_MAX_LAYERS) {
            ImGui::TextDisabled(t("ui.lut_panel.stack_is_full"));
        } else {
            static char lutSearch[256] = {};
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##lut_search", t("ui.lut_panel.search_luts"), lutSearch, sizeof(lutSearch));
            ImGui::PopItemWidth();
            std::string needle = lutSearch;
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });
            int shown = 0;
            for (const auto& e : catalog) {
                if (e.kind == LutEntryType::Error) {
                    continue;
                }
                if (!needle.empty()) {
                    std::string hay = e.displayName;
                    std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) {
                        return (char)::tolower(c);
                    });
                    if (hay.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                ++shown;
            }
            ImGui::TextDisabled(t("ui.lut_panel.match_count"), shown, shown == 1 ? "" : "es");
            ImGui::BeginChild("##lut_add_list", ImVec2(320, 200));
            // collect matches once, then clip
            static std::vector<int> lutMatches;
            lutMatches.clear();
            lutMatches.reserve(shown);
            for (size_t ci = 0; ci < catalog.size(); ++ci) {
                const auto& e = catalog[ci];
                const bool isErr = (e.kind == LutEntryType::Error);
                if (!isErr && !needle.empty()) {
                    std::string hay = e.displayName;
                    std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) {
                        return (char)::tolower(c);
                    });
                    if (hay.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                lutMatches.push_back((int)ci);
            }
            ImGuiListClipper clipper;
            clipper.Begin((int)lutMatches.size());
            while (clipper.Step()) {
                for (int ri = clipper.DisplayStart; ri < clipper.DisplayEnd; ++ri) {
                    const auto& e = catalog[lutMatches[ri]];
                    const bool isErr = (e.kind == LutEntryType::Error);
                    std::string label = e.displayName;
                    if (isErr) {
                        label += " (!)";
                    }
                    ImGui::BeginDisabled(isErr);
                    if (ImGui::Selectable((label + "##lut_" + std::to_string(lutMatches[ri])).c_str())) {
                        int sub = -1;
                        if (e.kind == LutEntryType::MLUTSub) {
                            sub = e.subIndex;
                        }
                        addLayer(makeLutRef(e.filename, sub));
                        selectedLayer = (int)layers.size() - 1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
    if (removeAt >= 0) {
        removeLayer((size_t)removeAt);
        if (selectedLayer >= (int)layers.size()) {
            selectedLayer = (int)layers.size() - 1;
        }
    }
}
