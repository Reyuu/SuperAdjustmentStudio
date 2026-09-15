#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "IconsFontAwesome6.h"
#include "helpers/raii_guards.h"
#include "helpers/toast_notifications.h"
#include "imgui.h"
#include "ui.h"
#include <LESDK/Includes.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>

#include "animation.h"
#include "application.h"
#include "bones.h"
#include "engine.h"
#include "game_window.h"
#include "gizmo.h"
#include "logger.h"
#include "mouse.h"
#include "native_renderer.h"
#include "pcc_parser.h"
#include "props.h"
#include "settings.h"
#include "tracy.h"

// TODO: translations (save needed strings to .po, header only utility, save as std::map?, set global language,
// gettext("english"))

static auto axisWidgetLambda = [](const char* label, const char* id, float* value, float step, float lo, float hi, const char* format) {
    ImGui::Text(label); // warning does not matter here
    ImGui::SameLine();
    return ImGui::DragFloat(id, value, step, lo, hi, format);
};

static const auto applyDebounced = [](bool& toApply, float& lastEdit, float& lastApply, auto apply, float burstWindow = 0.35f, float burstInterval = 0.25f,
                                      float idleInterval = 0.05f) {
    if (!toApply) {
        return;
    }
    const float now = ImGui::GetTime();
    const bool inBurst = (now - lastEdit) < burstWindow;
    if (now - lastApply >= (inBurst ? burstInterval : idleInterval)) {
        apply();
        lastApply = now;
        if (!inBurst) {
            toApply = false;
        }
    }
};

void UI::refreshBoneList(const std::string& pawnName) {
    bones.clear();
    boneIndex = 0;
    Application::instance().bones().listBones(pawnName, (MeshTarget)meshTargetIndex, bones);
    if (!bones.empty()) {
        BonePoseInfo b;
        if (Application::instance().bones().getBoneTransform(pawnName, (MeshTarget)meshTargetIndex, bones[0].index, b)) {
            boneEdit = b;
        }
    }
}

bool UI::getSelectedBone(std::string& pawn, int& target, int& listPos, int& boneIndexOut) {
    if (bones.empty() || boneIndex < 0 || boneIndex >= (int)bones.size() || bonePawn.empty()) {
        return false;
    }
    pawn = bonePawn;
    target = meshTargetIndex;
    listPos = boneIndex;
    boneIndexOut = bones[boneIndex].index;
    return true;
}

// check if the inheritance chain contains AActor class
static bool isActorClass(UClass* cls) {
    for (UClass* s = cls; s; s = (UClass*)s->SuperField) {
        if (s == AActor::StaticClass()) {
            return true;
        }
    }
    return false;
}

void UI::collectPawns() {
    ZoneScopedN("UI::collectPawns");
    std::string sel = (!pawnNamesVector.empty() && pawnIndexInt >= 0 && pawnIndexInt < (int)pawnNamesVector.size()) ? pawnNamesVector[pawnIndexInt] : "";
    pawnNamesVector.clear();
    pawnNamesLowerVector.clear();
    pawnIndexInt = 0;
    forEachOf<AActor>([&](AActor* obj) {
        if (!advancedSelection && !obj->IsA(APawn::StaticClass())) {
            return;
        }

        std::string nm = FStringToUtf8(obj->GetName());
        if (nm.rfind("Default__", 0) == 0) {
            return;
        }
        pawnNamesVector.push_back(nm);
    });

    // actors we spawned
    for (const std::string& sn : Application::instance().engine().spawnedNames()) {
        if (std::find(pawnNamesVector.begin(), pawnNamesVector.end(), sn) != pawnNamesVector.end()) {
            continue;
        }
        if (Application::instance().engine().findActorByName(sn)) {
            pawnNamesVector.push_back(sn);
        }
    }

    // object pinned by click-to-select
    for (const std::string& pn : pinnedNamesVector) {
        if (std::find(pawnNamesVector.begin(), pawnNamesVector.end(), pn) != pawnNamesVector.end()) {
            continue;
        }
        if (Application::instance().engine().findActorByName(pn)) {
            pawnNamesVector.push_back(pn);
        }
    }

    pawnNamesLowerVector.clear();
    pawnNamesLowerVector.reserve(pawnNamesVector.size());
    for (const std::string& n : pawnNamesVector) {
        pawnNamesLowerVector.push_back(toLowerStr(n));
    }

    if (!sel.empty()) {
        for (int i = 0; i < (int)pawnNamesVector.size(); i++) {
            if (pawnNamesVector[i] == sel) {
                pawnIndexInt = i;
                break;
            }
        }
    }
}

// select actor by pointer
void UI::selectActor(AActor* actor) {
    if (!actor) {
        return;
    }
    // since clicked object can be any type -> forcefully turn on advanced selection
    // if (!advancedSelection) {
    //     advancedSelection = true;
    //     collectPawns();
    // }

    std::string nm = FStringToUtf8(actor->GetName());
    int index = -1;
    for (int i = 0; i < (int)pawnNamesVector.size(); ++i) {
        if (pawnNamesVector[i] == nm) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        pawnNamesVector.push_back(nm);
        pawnNamesLowerVector.push_back(toLowerStr(nm));
        index = (int)pawnNamesVector.size() - 1;
    }
    pawnIndexInt = index;
    if (std::find(pinnedNamesVector.begin(), pinnedNamesVector.end(), nm) == pinnedNamesVector.end()) {
        pinnedNamesVector.push_back(nm);
    }
    Application::instance().gizmo().setTarget(actor);
}

void UI::collectClasses() {
    ZoneScopedN("UI::collectClasses");
    std::string sel = selectedClassFullName;
    classes.clear();
    classIndex = 0;
    int excludedAbstract = 0;
    if (!UObject::GObjObjects) {
        return;
    }
    // lmao
    forEachOf<UClass>([&](UClass* cls) {
        if (!isActorClass(cls)) {
            return;
        }
        if (cls->ClassFlags & CLASS_Abstract) {
            ++excludedAbstract;
            return;
        }

        ClassEntry e;
        e.fullName = FStringToUtf8(cls->GetFullName());
        e.name = FStringToUtf8(cls->GetName());
        UObject* outer = cls->Outer;
        while (outer && outer->Outer) {
            outer = outer->Outer;
        }
        if (outer) {
            e.package = FStringToUtf8(outer->GetName());
        }
        if (e.package.empty()) {
            e.package = "(unknown)";
        }
        e.fullNameLower = toLowerStr(e.fullName);
        e.packageLower = toLowerStr(e.package);
        classes.push_back(e);
    });

    // naive sort, first by package, then by name
    std::sort(classes.begin(), classes.end(), [](const ClassEntry& a, const ClassEntry& b) {
        if (a.package != b.package) {
            return a.package < b.package;
        }
        return a.name < b.name;
    });

    if (!sel.empty()) {
        for (int i = 0; i < (int)classes.size(); ++i) {
            if (classes[i].fullName == sel) {
                classIndex = i;
                break;
            }
        }
    }

    static bool excludedAbstractLogged = false;
    if (excludedAbstract > 0 && !excludedAbstractLogged) {
        excludedAbstractLogged = true;
        std::ostringstream ss;
        ss << "collectClasses: excluded " << excludedAbstract << " abstract (non-spawnable) classes";
        Logger->debug(ss.str());
    }
}

void UI::collectPackages() {
    ZoneScopedN("UI::collectPackages");
    std::vector<PccFile> files = collectGamePccFiles();
    packages.clear();
    packages.reserve(files.size());
    for (const PccFile& f : files) {
        // path()/stem() must be split on the filesystem's preferred separator
        // (Windows backslash); std::filesystem::path handles that.
        std::string name = std::filesystem::path(f.path).stem().string();
        packages.push_back({f.path, f.relPath, name, toLowerStr(name)});
    }
    if (packages.empty()) {
        packageIndex = 0;
        return;
    }
    if (packageIndex < 0 || packageIndex >= (int)packages.size()) {
        packageIndex = 0;
    }
    Logger->info("collectPackages: found " + std::to_string(packages.size()) + " .pcc packages");
}

void UI::collectPackagesAsync() {
    ZoneScopedN("UI::collectPackagesAsync");
    std::lock_guard<std::mutex> lk(packagesMutex);
    if (packagesCollecting.load()) {
        return;
    }
    packagesCollecting.store(true);
    packagesFuture = std::async(std::launch::async, []() -> std::vector<PackageEntry> {
        ZoneScopedN("collectPackages worker");
        std::vector<PccFile> files = collectGamePccFiles();
        std::vector<PackageEntry> out;
        out.reserve(files.size());
        for (const PccFile& f : files) {
            std::string name = std::filesystem::path(f.path).stem().string();
            out.push_back({f.path, f.relPath, name, toLowerStr(name)});
        }
        TracyMessageL("collectPackages worker done");
        return out;
    });
}

void UI::pollPackagesAsync() {
    ZoneScopedN("UI::pollPackagesAsync");
    std::lock_guard<std::mutex> lk(packagesMutex);
    if (!packagesCollecting.load() || !packagesFuture.valid()) {
        return;
    }
    if (packagesFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    try {
        auto result = packagesFuture.get();
        packages = std::move(result);
        if (packages.empty()) {
            packageIndex = 0;
        } else if (packageIndex < 0 || packageIndex >= (int)packages.size()) {
            packageIndex = 0;
        }
        Logger->info("collectPackagesAsync: found " + std::to_string(packages.size()) + " .pcc packages (async)");
    } catch (const std::exception& e) {
        Logger->info(std::string("collectPackagesAsync: worker threw: ") + e.what());
    } catch (...) {
        Logger->info("collectPackagesAsync: worker threw unknown");
    }
    packagesCollecting.store(false);
}

void UI::collectAnimations(const std::string& pawnName) {
    animationNames.clear();
    animationIndex = 0;
    USkeletalMeshComponent* mesh = Application::instance().engine().findPawnMesh(pawnName);
    if (!mesh) {
        return;
    }

    auto addSeq = [this](UAnimSequence* seq) {
        if (!seq) {
            return;
        }

        const char* nm = seq->SequenceName.GetName();
        if (!nm || !*nm) {
            return;
        }

        std::string s(nm);
        if (std::find(animationNames.begin(), animationNames.end(), s) == animationNames.end()) {
            animationNames.push_back(s);
        }
    };

    for (int i = 0; i < (int)mesh->AnimSets.Count(); ++i) {
        UAnimSet* set = mesh->AnimSets.GetData()[i];
        if (!set) {
            continue;
        }
        for (int j = 0; j < (int)set->Sequences.Count(); ++j) {
            addSeq(set->Sequences.GetData()[j]);
        }
    }

    // try to load other animation sets, that aren't linked to the mesh component
    if (animationIncludeAll) {
        forEachOf<UAnimSet>([&](UAnimSet* set) {
            for (int j = 0; j < (int)set->Sequences.Count(); ++j) {
                addSeq(set->Sequences.GetData()[j]);
            }
        });
    }
}

bool UI::renderTransformEditor(Transform& t, const char* idPrefix) {
    bool edited = false;

    ImGui::PushID(idPrefix);
    ImGui::Text(t("ui.transform_table.position"));
    ImGui::PushItemWidth(-100);
    edited |= axisWidgetLambda("X", "##px", &t.pos[0], 1.0f, -100000.f, 100000.f, "%.1f");
    edited |= axisWidgetLambda("Y", "##py", &t.pos[1], 1.0f, -100000.f, 100000.f, "%.1f");
    edited |= axisWidgetLambda("Z", "##pz", &t.pos[2], 1.0f, -100000.f, 100000.f, "%.1f");
    ImGui::Text(t("ui.transform_table.rotation"));
    edited |= axisWidgetLambda("RX", "##rx", &t.rot[0], 0.1f, -180.f, 180.f, "%.1f");
    edited |= axisWidgetLambda("RY", "##ry", &t.rot[1], 0.1f, -180.f, 180.f, "%.1f");
    edited |= axisWidgetLambda("RZ", "##rz", &t.rot[2], 0.1f, -180.f, 180.f, "%.1f");
    ImGui::Text(t("ui.transform_table.scale"));
    edited |= axisWidgetLambda("X", "##sx", &t.scale[0], 0.01f, 0.001f, 100.f, "%.3f");
    edited |= axisWidgetLambda("Y", "##sy", &t.scale[1], 0.01f, 0.001f, 100.f, "%.3f");
    edited |= axisWidgetLambda("Z", "##sz", &t.scale[2], 0.01f, 0.001f, 100.f, "%.3f");
    ImGui::PopItemWidth();
    ImGui::PopID();
    return edited;
}

void UI::applyUIInputState(GameWindow& window) {
    const bool show = showUIstate.load();
    // only show system cursor while overlay is shown
    while (show ? (ShowCursor(TRUE) < 0) : (ShowCursor(FALSE) >= 0)) {
    }

    Application::instance().engine().freezeLook(show);
    if (show) {
        Application::instance().mouse().cursorPassthrough() = true;
        GetCursorPos(&Application::instance().mouse().frozenCursor());
        Application::instance().mouse().cursorPassthrough() = false;

        if (window.primary()) {
            SetCapture(window.primary());
            RECT rc;
            if (GetClientRect(window.primary(), &rc)) {
                POINT tl{0, 0};
                ClientToScreen(window.primary(), &tl);
                RECT clip{tl.x, tl.y, tl.x + rc.right, tl.y + rc.bottom};
                ClipCursor(&clip);
            }
        }
    } else {
        // Preserve freecam drag state when closing overlay
        if (!Application::instance().freecam().isCameraDragActive().load()) {
            Application::instance().engine().isCameraDragActive() = false;
        }
        ClipCursor(NULL);
        ReleaseCapture();
    }
}

// main function to render the overlay contents
void UI::renderOverlayContents(NativeRenderer& renderer) {
    ZoneScopedN("UI::renderOverlayContents");
    // poll any in-flight package scan (non-blocking, Tracy-tracked worker)
    pollPackagesAsync();
    if (pawnNamesVector.empty()) {
        collectPawns();
    }

    Application::instance().engine().freezeLook(true); // lock the camera on the engine level
    if (ImGui::GetTime() - lastPawnRefresh > 2.0f) {
        lastPawnRefresh = ImGui::GetTime();
        collectPawns();
    }
    if (classes.empty()) {
        collectClasses();
    }
    if (ImGui::GetTime() - lastClassRefresh > 5.0f) {
        lastClassRefresh = ImGui::GetTime();
        collectClasses();
    }
    if (packages.empty() && !packagesCollecting.load()) {
        collectPackagesAsync();
    }
    if (ImGui::GetTime() - lastPackageRefresh > 10.0f && !packagesCollecting.load()) {
        lastPackageRefresh = ImGui::GetTime();
        collectPackagesAsync();
    }
    if (pawnIndexInt < 0) {
        pawnIndexInt = 0;
    }
    if (!pawnNamesVector.empty() && pawnIndexInt >= (int)pawnNamesVector.size()) {
        pawnIndexInt = (int)pawnNamesVector.size() - 1;
    }
    if (pawnNamesVector.empty()) {
        pawnIndexInt = 0;
    }
    if (pendingCollectPawns) {
        pendingCollectPawns = false;
        collectPawns();
    }
    if (classIndex < 0) {
        classIndex = 0;
    }
    if (!classes.empty() && classIndex >= (int)classes.size()) {
        classIndex = (int)classes.size() - 1;
    }
    if (animationIndex < 0) {
        animationIndex = 0;
    }
    if (!animationNames.empty() && animationIndex >= (int)animationNames.size()) {
        animationIndex = (int)animationNames.size() - 1;
    }
    if (boneIndex < 0) {
        boneIndex = 0;
    }
    if (!bones.empty() && boneIndex >= (int)bones.size()) {
        boneIndex = (int)bones.size() - 1;
    }

    ImGui::SetNextWindowSize(ImVec2(650, 1250), ImGuiCond_FirstUseEver);

    ImGui::Begin((std::string(ICON_FA_SLIDERS " ") + t("ui.program_name") + " " PLUGIN_VERSION).c_str(), NULL, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        ImGui::MenuItem((std::string(ICON_FA_GEAR " ") + t("ui.settings")).c_str(), nullptr, &showSettingsWindow);
        Application::instance().snapshots().renderUi();
        if (ImGui::BeginMenu((std::string(ICON_FA_BUG " ") + t("ui.debug")).c_str())) {
            ImGui::MenuItem("Metrics##imgui_debug_metrics", (std::string(ICON_FA_CHART_SIMPLE " ") + t("ui.metrics_menu.metrics")).c_str(), &showMetricsWindow);
            ImGui::MenuItem("Debug Log##imgui_debug_log", (std::string(ICON_FA_LIST " ") + t("ui.metrics_menu.debug_log")).c_str(), &showDebugLogWindow);
            ImGui::MenuItem("ID Stack##imgui_id_stack", (std::string(ICON_FA_LAYER_GROUP " ") + t("ui.metrics_menu.id_stack")).c_str(), &showIDStackToolWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    Application::instance().snapshots().renderWizards();
    ImVec2 windowPosition = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    renderer.setUiRect({(LONG)windowPosition.x, (LONG)windowPosition.y, (LONG)(windowPosition.x + windowSize.x), (LONG)(windowPosition.y + windowSize.y)});

    if (Application::instance().animation().animPauseActive()) {
        Application::instance().animation().keepAnimationsPaused();
    }
    // DO NOT run Bones:keepBonePoses here -> SHOULD ALWAYS RUN ON GAME THREAD ABioHUD::PostRender

    renderControlsSection();
    Application::instance().freecam().renderUi();
    renderSelectionSection();
    renderSpawnSection();
    renderPackagesSection();

    ImGui::End();

    if (showMetricsWindow) {
        ImGui::ShowMetricsWindow(&showMetricsWindow);
    }
    if (showDebugLogWindow) {
        ImGui::ShowDebugLogWindow(&showDebugLogWindow);
    }
    if (showIDStackToolWindow) {
        ImGui::ShowIDStackToolWindow(&showIDStackToolWindow);
    }
    if (showSettingsWindow) {
        Application::instance().settings().renderSettingsWindow(&showSettingsWindow);
    }

    toastManager.renderToastNotifications();
    Application::instance().properties().renderStructWindows();
}

#pragma region //  CONTROLS
void UI::renderControlsSection() {
    ImGui::Text(t("ui.controls"));

    ImGui::BeginTable("primary_controls##primary_controls", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    const char* icon;

    icon = pauseTime ? ICON_FA_PAUSE : ICON_FA_PLAY;
    if (ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.pause")).c_str(), &pauseTime)) {
        Application::instance().engine().setPause(pauseTime);
    }

    ImGui::TableNextColumn();
    // i have a theory, it doesn't work here, cause it should be run on PostRender, instead of Present...
    bool hideGameUI = Application::instance().engine().isGameUIHidden();
    icon = hideGameUI ? ICON_FA_EYE_SLASH : ICON_FA_EYE;
    if (ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.hide_all")).c_str(), &hideGameUI)) {
        Application::instance().engine().setGameUIHidden(hideGameUI);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = Application::instance().gizmo().clickSelect() ? ICON_FA_ARROW_POINTER : ICON_FA_HAND_POINTER;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.click_to_select")).c_str(), &Application::instance().gizmo().clickSelect());
    ImGui::TableNextColumn();

    icon = advancedSelection ? ICON_FA_HAND : ICON_FA_HAND_BACK_FIST;
    if (ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.advanced_selection")).c_str(), &advancedSelection)) {
        pendingCollectPawns = true;
    }

    // so it only makes sense to apply it when:
    // - the collection is not empty
    // - we actually have a pawn selected
    if (!pawnNamesVector.empty() && pawnIndexInt >= 0 && pawnIndexInt < (int)pawnNamesVector.size()) {
        if (floatPawn != pawnNamesVector[pawnIndexInt]) {
            floatPawn = pawnNamesVector[pawnIndexInt];
            floatEnabled = false;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        icon = ICON_FA_PERSON_ARROW_UP_FROM_LINE;
        if (ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.float_selected")).c_str(), &floatEnabled)) {
            Application::instance().engine().setFloat(floatPawn, floatEnabled);
        }
    }

    ImGui::TableNextColumn();
    icon = ICON_FA_TRIANGLE_EXCLAMATION;
    if (ImGui::Checkbox((std::string(icon) + " " + t("ui.controls_table.allow_slack")).c_str(),
                        &Application::instance().properties().allowArraySlackExpansion)) {
        if (Application::instance().properties().allowArraySlackExpansion) {
            toastManager.addToastNotification(t("ui.controls_table.allow_slack_warning"), ToastTypeWarning, 3.0);
        }
    }

    ImGui::EndTable();

    ImGui::Separator();

    ImGui::BeginTable("gizmos_table##gizmos_table", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = ICON_FA_CUBE;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.gizmos_table.show_orient")).c_str(), &Application::instance().gizmo().showGizmo());
    ImGui::TableNextColumn();
    icon = ICON_FA_LAYER_GROUP;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.gizmos_table.always_on_top")).c_str(), &Application::instance().gizmo().debugAlwaysOnTop());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = ICON_FA_ARROWS_TO_DOT;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.gizmos_table.draw_tracer")).c_str(), &Application::instance().gizmo().drawTracer());
    ImGui::TableNextColumn();
    icon = ICON_FA_HIGHLIGHTER;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.gizmos_table.highlight")).c_str(), &Application::instance().gizmo().highlightSelected());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = ICON_FA_BONE;
    ImGui::Checkbox((std::string(icon) + " " + t("ui.gizmos_table.bone_pivot")).c_str(), &Application::instance().gizmo().showBonePivot());
    ImGui::EndTable();
    ImGui::Separator();
}
#pragma endregion

#pragma region //  SELECTION
void UI::renderSelectionSection() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_OBJECT_GROUP " ") + t("ui.selection")).c_str())) {
        return;
    }
    ImGui::Indent();
    renderSelectionTarget();
    renderSelectionTransform();
    renderSelectionAnimation();
    Application::instance().vfx().renderUI();
    renderSelectionBones();
    renderSelectionOtherProps();
    ImGui::Unindent();
}

void UI::renderSelectionTarget() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_CROSSHAIRS " ") + t("ui.selection_table.target") + "##sel").c_str())) {
        return;
    }
    ImGui::Indent();

    if (!pawnNamesVector.empty() && (pawnIndexInt < 0 || pawnIndexInt >= (int)pawnNamesVector.size())) {
        pawnIndexInt = 0;
    }
    const char* pawnPreview = (pawnNamesVector.empty() || pawnIndexInt < 0 || pawnIndexInt >= (int)pawnNamesVector.size())
                                  ? t("ui.selection_table.no_objects")
                                  : pawnNamesVector[pawnIndexInt].c_str();
    // ImGui::Text(ICON_FA_CROSSHAIRS " Target");
    // ImGui::SameLine();

    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    static char targetSearchFilter[256] = "";
    ImGui::InputText("##class_search_target_search", targetSearchFilter, sizeof(targetSearchFilter));
    std::string filterLower = toLowerStr(targetSearchFilter);
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // might change this
    // only removes a pawn that we spawned, since removing for example Player pawn would be...
    // disastrous
    auto& spawnedNames = Application::instance().engine().spawnedNames();
    bool isFoundInCollection = false;
    bool isSpawned = false;
    if (!pawnNamesVector.empty() && pawnIndexInt >= 0 && pawnIndexInt < (int)pawnNamesVector.size()) {
        const std::string& selectedPawn = pawnNamesVector[pawnIndexInt];
        isFoundInCollection = std::find(spawnedNames.begin(), spawnedNames.end(), selectedPawn) != spawnedNames.end();
        isSpawned = isFoundInCollection;
        if (ImGui::Button(ICON_FA_TRASH "##sel_target_trash") && isSpawned && !advancedSelection) {
            Application::instance().engine().removeActor(selectedPawn);
        }
    } else {
        ImGui::Button(ICON_FA_TRASH "##sel");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##sel_target_refresh")) {
        collectPawns();
    }

    if (ImGui::Button((std::string(ICON_FA_PERSON " ") + t("ui.selection_table.target_player") + "##sel_target").c_str(),
                      ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        AActor* player = Application::instance().engine().playerPawn();
        if (player) {
            selectActor(player);
        }
    }

    {
        ChildScope child("sel_list", ImVec2(0, 120), true);
        ImGui::PushItemWidth(-100);
        if (child.open) {
            if (filterLower.empty()) {
                ImGuiListClipper clipper;
                clipper.Begin((int)pawnNamesVector.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        std::string id = pawnNamesVector[i] + "##" + std::to_string(i);
                        if (ImGui::Selectable(id.c_str(), i == pawnIndexInt)) {
                            pawnIndexInt = i;
                            Application::instance().gizmo().clearExplicitTarget();
                        }
                    }
                }
            } else {
                // filtered: use cached lower vector to avoid per-item toLower
                static std::vector<int> filteredPawns;
                filteredPawns.clear();
                filteredPawns.reserve(pawnNamesVector.size());
                for (int i = 0; i < (int)pawnNamesVector.size(); ++i) {
                    const std::string& lower = (i < (int)pawnNamesLowerVector.size()) ? pawnNamesLowerVector[i] : toLowerStr(pawnNamesVector[i]);
                    if (lower.find(filterLower) != std::string::npos) {
                        filteredPawns.push_back(i);
                    }
                }
                if (filteredPawns.empty()) {
                    ImGui::TextDisabled(t("ui.selection_table.no_matches"));
                } else {
                    ImGuiListClipper clipper;
                    clipper.Begin((int)filteredPawns.size());
                    while (clipper.Step()) {
                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                            int i = filteredPawns[n];
                            std::string id = pawnNamesVector[i] + "##" + std::to_string(i);
                            if (ImGui::Selectable(id.c_str(), i == pawnIndexInt)) {
                                pawnIndexInt = i;
                                Application::instance().gizmo().clearExplicitTarget();
                            }
                        }
                    }
                }
            }
        }
        ImGui::PopItemWidth();
    }

    ImGui::Unindent();
}

// live! (250ms debounce)
void UI::renderSelectionTransform() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " ") + t("ui.transform") + "##sel").c_str())) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled(t("ui.transform_table.apply_desc"));
    AActor* target = Application::instance().gizmo().target();
    if (target) {
        std::string targetName = FStringToUtf8(target->GetName());
        if (transformPawn != targetName && !transformToApply) {
            transformPawn = targetName;
            Application::instance().engine().loadTransformFromActor(target, selectedTransform);
        }
    } else if (pawnNamesVector.empty()) {
        transformPawn.clear();
    }

    if (ImGui::Button((std::string(ICON_FA_ARROW_RIGHT_TO_BRACKET " ") + t("ui.transform_table.reload") + "##sel").c_str())) {
        if (target) {
            transformPawn = FStringToUtf8(target->GetName());
            Application::instance().engine().loadTransformFromActor(target, selectedTransform);
            transformToApply = false;
            char reloadBuf[256];
            snprintf(reloadBuf, sizeof(reloadBuf), t("ui.transform_table.reloaded"), transformPawn.c_str());
            toastManager.addToastNotification(reloadBuf, ToastTypeSuccess, 2.0);
        }
    }

    bool tfEdited = renderTransformEditor(selectedTransform, "sel");
    if (tfEdited) {
        transformToApply = true;
        transformLastApply = ImGui::GetTime();
    }
    if (transformToApply && target) {
        applyDebounced(transformToApply, transformLastEdit, transformLastApply, [&] {
            AActor* t = target;
            Transform tr = selectedTransform;
            Application::instance().engine().postGameThreadTask([t, tr]() {
                if (!isLiveObject(t)) {
                    return;
                }
                Application::instance().engine().setTransform(t, tr);
            });
        });
    }
    ImGui::Unindent();
}

void UI::renderSelectionAnimation() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_FILM " ") + t("ui.animation")).c_str())) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled(t("ui.animation_table.apply_desc"));

    if (!pawnNamesVector.empty()) {
        if (animationPawn != pawnNamesVector[pawnIndexInt]) {
            animationPawn = pawnNamesVector[pawnIndexInt];
            collectAnimations(animationPawn);
        }
    } else {
        animationPawn.clear();
        animationNames.clear();
        animationIndex = 0;
    }

    // TODO: Improve the search, it's VERY picky atm
    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##anim_search", animationSearch, sizeof(animationSearch));
    ImGui::PopItemWidth();
    std::string aFilter = toLowerStr(animationSearch);
    {
        ChildScope child("anim_list", ImVec2(0, 120), true);
        if (child.open) {
            if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##anim_refresh")) {
                collectAnimations(animationPawn);
            }
            ImGui::Separator();

            int aShown = 0;
            if (aFilter.empty()) {
                aShown = (int)animationNames.size();
                ImGuiListClipper clipper;
                clipper.Begin((int)animationNames.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        std::string id = animationNames[i] + "##" + std::to_string(i);
                        if (ImGui::Selectable(id.c_str(), i == animationIndex)) {
                            animationIndex = i;
                        }
                    }
                }
            } else {
                // build indices first to allow clipping
                static std::vector<int> filtered;
                filtered.clear();
                filtered.reserve(animationNames.size());
                for (int i = 0; i < (int)animationNames.size(); ++i) {
                    if (toLowerStr(animationNames[i]).find(aFilter) != std::string::npos) {
                        filtered.push_back(i);
                    }
                }
                aShown = (int)filtered.size();
                ImGuiListClipper clipper;
                clipper.Begin((int)filtered.size());
                while (clipper.Step()) {
                    for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                        int i = filtered[n];
                        std::string id = animationNames[i] + "##" + std::to_string(i);
                        if (ImGui::Selectable(id.c_str(), i == animationIndex)) {
                            animationIndex = i;
                        }
                    }
                }
            }
            if (aShown == 0) {
                ImGui::Text(t("ui.animation_table.no_matches"));
            }
        }
    }

    ImGui::Text((std::string(ICON_FA_CROSSHAIRS " ") + t("ui.animation_table.selected_animation")).c_str());
    ImGui::SameLine();
    ImGui::Text("%s", animationNames.empty() ? t("ui.animation_table.none") : animationNames[animationIndex].c_str());

    ImGui::Separator();
    bool canPlay = !pawnNamesVector.empty() && !animationNames.empty();
    if (ImGui::Button((std::string(ICON_FA_PLAY " ") + t("ui.animation_table.play")).c_str()) && canPlay) {
        Application::instance().animation().playAnimation(pawnNamesVector[pawnIndexInt], animationNames[animationIndex], false);
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_REPEAT " ") + t("ui.animation_table.play_loop")).c_str()) && canPlay) {
        Application::instance().animation().playAnimation(pawnNamesVector[pawnIndexInt], animationNames[animationIndex], true);
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_ROTATE_RIGHT " ") + t("ui.animation_table.reset")).c_str()) && !pawnNamesVector.empty()) {
        Application::instance().animation().resetAnimation(pawnNamesVector[pawnIndexInt]);
    }

    if (ImGui::Checkbox(t("ui.animation_table.include_all"), &animationIncludeAll)) {
        animationIndex = 0;
        if (!pawnNamesVector.empty()) {
            collectAnimations(animationPawn);
        }
    }

    ImGui::Unindent();
}

#pragma endregion

#pragma region // BONES
void UI::renderSelectionBones() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_BONE " ") + t("ui.bones")).c_str())) {
        return;
    }
    ImGui::Indent();

    if (pawnNamesVector.empty()) {
        renderBonesReset();
        ImGui::Unindent();
        return;
    }

    const std::string& pawn = pawnNamesVector[pawnIndexInt];
    AActor* poseActor = Application::instance().engine().findActorByName(pawn);
    bool isBioPawn = poseActor && poseActor->IsA(ABioPawn::StaticClass());

    if (bonePawn != pawn) {
        boneListTried = false;
        Application::instance().bones().resetBonePose(bonePawn, (MeshTarget)meshTargetIndex);
        bonePawn = pawn;
        bones.clear();
        boneIndex = 0;
    }

    int maxTarget = isBioPawn ? 1 : 0;
    if (meshTargetIndex > maxTarget) {
        meshTargetIndex = 0;
    }

    const std::string meshBody = Translation::instance().translate("ui.bones_table.body");
    const std::string meshHead = Translation::instance().translate("ui.bones_table.head");
    const char* meshTargets[2] = {meshBody.c_str(), meshHead.c_str()};
    int oldTarget = meshTargetIndex;
    ImGui::Text(t("ui.bones_table.target_mesh"));

    ImGui::Text(ICON_FA_CROSSHAIRS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    if (ImGui::Combo("##bones", &meshTargetIndex, meshTargets, maxTarget + 1)) {
        if (meshTargetIndex != oldTarget) {
            boneListTried = false;
            Application::instance().bones().resetBonePose(bonePawn, (MeshTarget)meshTargetIndex);
            refreshBoneList(pawn);
        }
    }
    ImGui::PopItemWidth();

    renderBonesDirectBones(pawn);
    ImGui::Unindent();
}

void UI::renderBonesDirectBones(const std::string& pawn) {
    if (bonePawn == pawn && bones.empty() && !boneListTried) {
        refreshBoneList(pawn);
        boneListTried = true;
    }
    if (bones.empty()) {
        ImGui::TextDisabled(t("ui.bones_table.no_bones"));
        return;
    }

    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##bonesdirect", boneSearch, sizeof(boneSearch));
    ImGui::PopItemWidth();

    std::string bFilter = toLowerStr(boneSearch);
    {
        ChildScope child("bones_dir_list", ImVec2(0, 180), true);
        if (child.open) {
            int bShown = 0;
            if (bFilter.empty()) {
                bShown = (int)bones.size();
                ImGuiListClipper clipper;
                clipper.Begin((int)bones.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const BonePoseInfo& b = bones[i];
                        std::string label = b.boneName;
                        if (label.empty()) {
                            char boneBuf[64];
                            snprintf(boneBuf, sizeof(boneBuf), t("ui.bones_table.named_bone"), std::to_string(b.index).c_str());
                            label = boneBuf;
                        }
                        if (!b.parentName.empty()) {
                            label += " < " + b.parentName;
                        }
                        if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), i == boneIndex)) {
                            boneIndex = i;
                            BonePoseInfo fresh;
                            if (Application::instance().bones().getBoneTransform(pawn, (MeshTarget)meshTargetIndex, b.index, fresh)) {
                                boneEdit = fresh;
                                boneEdit.pos[0] = boneEdit.pos[1] = boneEdit.pos[2] = 0.0f;
                                boneToApply = false;
                            }
                        }
                    }
                }
            } else {
                static std::vector<int> filteredBones;
                filteredBones.clear();
                filteredBones.reserve(bones.size());
                for (int i = 0; i < (int)bones.size(); ++i) {
                    const BonePoseInfo& b = bones[i];
                    std::string label = b.boneName;
                    if (label.empty()) {
                        label = "(bone " + std::to_string(b.index) + ")";
                    }
                    if (!b.parentName.empty()) {
                        label += " < " + b.parentName;
                    }
                    if (toLowerStr(label).find(bFilter) != std::string::npos) {
                        filteredBones.push_back(i);
                    }
                }
                bShown = (int)filteredBones.size();
                ImGuiListClipper clipper;
                clipper.Begin((int)filteredBones.size());
                while (clipper.Step()) {
                    for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                        int i = filteredBones[n];
                        const BonePoseInfo& b = bones[i];
                        std::string label = b.boneName;
                        if (label.empty()) {
                            char boneBuf[64];
                            snprintf(boneBuf, sizeof(boneBuf), t("ui.bones_table.named_bone"), std::to_string(b.index).c_str());
                            label = boneBuf;
                        }
                        if (!b.parentName.empty()) {
                            label += " < " + b.parentName;
                        }
                        if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), i == boneIndex)) {
                            boneIndex = i;
                            BonePoseInfo fresh;
                            if (Application::instance().bones().getBoneTransform(pawn, (MeshTarget)meshTargetIndex, b.index, fresh)) {
                                boneEdit = fresh;
                                boneEdit.pos[0] = boneEdit.pos[1] = boneEdit.pos[2] = 0.0f;
                                boneToApply = false;
                            }
                        }
                    }
                }
            }
            if (bShown == 0) {
                ImGui::Text(t("ui.bones_table.no_match"));
            } else {
                ImGui::Text(t("ui.bones_table.n_bones"), (int)bones.size());
            }
        }
    }
    ImGui::Separator();

    const BonePoseInfo& sel = bones[boneIndex];
    ImGui::Text(t("ui.bones_table.selected"), sel.boneName.c_str());
    if (!sel.parentName.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled(t("ui.bones_table.parent_bone"), sel.parentName.c_str());
    }
    ImGui::Separator();
    ImGui::PushItemWidth(-100);
    ImGui::Text(t("ui.bones_table.position_offset"));
    bool bEdited = axisWidgetLambda("X", "##dir_px", &boneEdit.pos[0], 0.5f, -10000.0f, 10000.0f, "%.2f");
    bEdited |= axisWidgetLambda("Y", "##dir_py", &boneEdit.pos[1], 0.5f, -10000.0f, 10000.0f, "%.2f");
    bEdited |= axisWidgetLambda("Z", "##dir_pz", &boneEdit.pos[2], 0.5f, -10000.0f, 10000.0f, "%.2f");
    ImGui::Text(t("ui.bones_table.rotation"));
    bEdited |= axisWidgetLambda("RX", "##dir_rx", &boneEdit.rot[0], 0.5f, -360.0f, 360.0f, "%.2f");
    bEdited |= axisWidgetLambda("RY", "##dir_ry", &boneEdit.rot[1], 0.5f, -360.0f, 360.0f, "%.2f");
    bEdited |= axisWidgetLambda("RZ", "##dir_rz", &boneEdit.rot[2], 0.5f, -360.0f, 360.0f, "%.2f");
    ImGui::Text(t("ui.bones_table.scale")); // FBoneAtom has no per-axis scale
    bEdited |= axisWidgetLambda("S", "##dir_scale", &boneEdit.scale[0], 0.01f, 0.001f, 100.0f, "%.3f");
    ImGui::PopItemWidth();

    // scale for direct bones is VERY weird, best to apply it for all axis
    // there's probably some method to the madness here.
    if (bEdited) {
        boneEdit.scale[1] = boneEdit.scale[2] = boneEdit.scale[0];
    }
    if (bEdited) {
        boneToApply = true;
        boneLastEdit = ImGui::GetTime();
    }
    if (boneToApply) {
        applyDebounced(boneToApply, boneLastEdit, boneLastApply, [&] {
            Application::instance().bones().setBonePose(pawn, (MeshTarget)meshTargetIndex, boneEdit);
        });
    }
    ImGui::Separator();
    if (ImGui::Button((std::string(ICON_FA_ARROW_ROTATE_RIGHT " ") + t("ui.bones_table.reload") + "##bones").c_str())) {
        BonePoseInfo fresh;
        if (Application::instance().bones().getBoneTransform(pawn, (MeshTarget)meshTargetIndex, sel.index, fresh)) {
            boneEdit = fresh;
            boneEdit.pos[0] = boneEdit.pos[1] = boneEdit.pos[2] = 0.0f;
            boneToApply = false;
            char boneReloadBuf[256];
            snprintf(boneReloadBuf, sizeof(boneReloadBuf), t("ui.bones_table.reloaded"), sel.boneName.c_str());
            toastManager.addToastNotification(boneReloadBuf, ToastTypeSuccess, 2.0);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_ARROW_ROTATE_RIGHT " ") + t("ui.bones_table.reset_from_pose") + "##bones_dir").c_str())) {
        Application::instance().bones().resetBonePose(pawn, (MeshTarget)meshTargetIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_LAND_MINE_ON " ") + t("ui.bones_table.absolute_reset") + "##bones").c_str())) {
        Application::instance().bones().absoluteResetBones(pawn, (MeshTarget)meshTargetIndex);
    }
}

void UI::renderBonesReset() {
    if (Application::instance().bones().bonePoseActive().load()) {
        Application::instance().bones().resetBonePose(bonePawn, (MeshTarget)meshTargetIndex);
    }

    bonePawn.clear();
    bones.clear();
    boneIndex = 0;
    boneListTried = false;
}
#pragma endregion

#pragma region // Other properties and Spawn
void UI::renderSelectionOtherProps() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_LIST " ") + t("ui.other") + "##sel").c_str())) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled(t("ui.other_table.apply_live"));

    Properties& props = Application::instance().properties();
    if (!pawnNamesVector.empty()) {
        AActor* propActor = Application::instance().engine().findActorByName(props.propertyPawn());
        if (props.propertyPawn() != pawnNamesVector[pawnIndexInt]) {
            props.propertyPawn() = pawnNamesVector[pawnIndexInt];
            props.propertyObject() = nullptr;
            props.propertyComponentIndex() = 0;
        }
        if (!propActor) {
            ImGui::Text(t("ui.other_table.not_found_target"));
        } else {
            // 0 -> self
            std::vector<UActorComponent*> components;
            Application::instance().gizmo().collectActorComponents(propActor, components);
            std::vector<std::string> cLabels;
            std::vector<const char*> cItems;
            cLabels.push_back("(actor) " + FStringToUtf8(propActor->GetName()));

            for (UActorComponent* c : components) {
                cLabels.push_back(FStringToUtf8(c->GetName()) + "[" + FStringToUtf8(c->Class->GetName()) + "]");
            }
            for (const std::string& s : cLabels) {
                cItems.push_back(s.c_str());
            }
            if (props.propertyComponentIndex() >= (int)cItems.size()) {
                props.propertyComponentIndex() = 0;
            }

            ImGui::Text(t("ui.other_table.target_comp"));
            ImGui::Text(ICON_FA_CROSSHAIRS);
            ImGui::SameLine();
            ImGui::PushItemWidth(-100);
            if (ImGui::Combo("##prop", &props.propertyComponentIndex(), cItems.data(), (int)cItems.size())) {
                UObject* t = (props.propertyComponentIndex() == 0) ? (UObject*)propActor : (UObject*)components[props.propertyComponentIndex() - 1];
                props.propertyObject() = t;
                props.collectProperties(t, props.properties());
            }
            ImGui::PopItemWidth();

            UObject* target = (props.propertyComponentIndex() == 0) ? (UObject*)propActor : (UObject*)components[props.propertyComponentIndex() - 1];
            if (target != props.propertyObject()) {
                props.closeStructWindows();
                props.propertyObject() = target;
                props.collectProperties(target, props.properties());
            }

            ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
            ImGui::SameLine();
            ImGui::PushItemWidth(-100);
            ImGui::InputText("##props", props.propertySearch(), 128);
            ImGui::PopItemWidth();

            if (ImGui::Button((std::string(ICON_FA_ARROW_ROTATE_RIGHT " ") + t("ui.other_table.reload_props")).c_str())) {
                props.propertyObject() = target;
                props.collectProperties(target, props.properties());
                char propsBuf[256];
                snprintf(propsBuf, sizeof(propsBuf), t("ui.other_table.reloaded_props"), FStringToUtf8(target->GetName()).c_str());
                toastManager.addToastNotification(propsBuf, ToastTypeSuccess, 2.0);
            }

            std::string pFilter = toLowerStr(props.propertySearch());
            ImGui::Text(t("ui.other_table.n_props_on_s"), (int)props.properties().size(), FStringToUtf8(target->GetName()).c_str());
            {
                ChildScope child("props_list", ImVec2(0, 300), true);
                if (child.open) {
                    props.renderPropertyTable(target, target, props.properties(), pFilter);
                }
            }
        }
    } else {
        props.propertyPawn().clear();
        props.propertyObject() = nullptr;
    }
    ImGui::Unindent();
}

void UI::renderSpawnSection() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_CUBES_STACKED " ") + t("ui.spawn")).c_str())) {
        return;
    }
    ImGui::Indent();
    ImColor redColor = ImColor(1.0f, 0.4f, 0.4, 1.0f);

    if (!UObject::GObjObjects) {
        ImGui::TextColored(redColor, t("ui.spawn_table.sdk_glob_err"));
    } else {
        renderSpawnClassList();
        renderSpawnTransform();
        Application::instance().lights().renderUI();
        Application::instance().prefabs().renderUI();
        Application::instance().particles().renderUI();
        renderSpawnOtherProps();
    }
    ImGui::Unindent();
}

void UI::renderSpawnClassList() {
    ZoneScopedN("UI::renderSpawnClassList");
    // simplification for the sake of the user
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_CUBES " ") + t("ui.spawn_table.object_list") + "##spawn").c_str())) {
        return;
    }
    ImGui::Indent();
    ImColor orangeColor = ImColor(1.0f, 0.6f, 0.2f, 1.0f);

    ImGui::TextColored(orangeColor, (std::string(ICON_FA_TRIANGLE_EXCLAMATION " ") + t("ui.spawn_table.warning")).c_str());

    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##class_search", classSearch, sizeof(classSearch));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT)) {
        collectClasses();
    }

    ImGui::Text(t("ui.spawn_table.selected_class"));
    ImGui::SameLine();
    ImGui::Text("%s", selectedClassFullName.empty() ? t("ui.spawn_table.none") : selectedClassFullName.c_str());

    std::string filter = toLowerStr(classSearch);
    {
        ChildScope child("class_list", ImVec2(0, 220), true);
        if (child.open) {
            std::string lastPkg;
            int shown = 0;
            for (int i = 0; i < (int)classes.size(); ++i) {
                const ClassEntry& e = classes[i];
                if (!filter.empty() && e.fullNameLower.find(filter) == std::string::npos && e.packageLower.find(filter) == std::string::npos) {
                    continue;
                }
                if (e.package != lastPkg) {
                    ImColor greyColor = ImColor(0.6f, 0.6f, 0.6f, 1.0f);
                    ImGui::TextColored(greyColor, "%s", e.package.c_str());
                    ImGui::Separator();
                    lastPkg = e.package;
                }
                if (ImGui::Selectable((e.name + "##" + std::to_string(i)).c_str(), i == classIndex)) {
                    classIndex = i;
                    selectedClassFullName = e.fullName;
                }
                ++shown;
            }
            if (shown == 0) {
                ImGui::Text(t("ui.spawn_table.no_classes"));
            }
        }
    }

    if (!selectedClassFullName.empty()) {
        // keep it bound
        Properties& props = Application::instance().properties();
        props.bindSpawnProperties(selectedClassFullName, false);
        if (ImGui::Button((std::string(ICON_FA_PLUS " ") + t("ui.spawn_table.spawn_selected")).c_str())) {
            AActor* spawned = Application::instance().engine().spawnClass(selectedClassFullName, spawnTransform);
            // auto-select spawned
            if (spawned) {
                props.applySpawnProperties(spawned, props.spawnProperties());
                std::string nm = FStringToUtf8(spawned->GetName());
                collectPawns();
                for (int i = 0; i < (int)pawnNamesVector.size(); ++i) {
                    if (pawnNamesVector[i] == nm) {
                        pawnIndexInt = i;
                        char spawnedBuf[512];
                        snprintf(spawnedBuf, sizeof(spawnedBuf), t("ui.spawn_table.spawned"), selectedClassFullName.c_str(), nm.c_str());
                        toastManager.addToastNotification(spawnedBuf, ToastTypeSuccess, 2.0);
                        break;
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_FA_STETHOSCOPE " ") + t("ui.spawn_table.diagnose")).c_str())) {
            char diagBuf[512];
            snprintf(diagBuf, sizeof(diagBuf), t("ui.spawn_table.diagnosing"), selectedClassFullName.c_str());
            toastManager.addToastNotification(diagBuf, ToastTypeInfo, 2.0);
            Logger->debug(Application::instance().engine().diagnoseClass(selectedClassFullName));
        }
    }
    ImGui::Unindent();
}

void UI::renderSpawnTransform() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " ") + t("ui.transform") + "##spawn").c_str())) {
        return;
    }
    ImGui::Indent();
    if (ImGui::Button((std::string(ICON_FA_ARROW_RIGHT_TO_BRACKET " ") + t("ui.transform_table.reload") + "##spawn").c_str())) {
        if (!pawnNamesVector.empty()) {
            Application::instance().engine().loadTransformFromPawn(pawnNamesVector[pawnIndexInt], spawnTransform);
            char reloadBuf[256];
            snprintf(reloadBuf, sizeof(reloadBuf), t("ui.transform_table.reloaded"), pawnNamesVector[pawnIndexInt].c_str());
            toastManager.addToastNotification(reloadBuf, ToastTypeSuccess, 2.0);
        }
    }
    renderTransformEditor(spawnTransform, "spawn");
    ImGui::Unindent();
}

void UI::renderSpawnOtherProps() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_LIST " ") + t("ui.other") + "##spawn").c_str())) {
        return;
    }
    ImGui::Indent();
    if (!selectedClassFullName.empty()) {
        Properties& props = Application::instance().properties();
        props.bindSpawnProperties(selectedClassFullName, false);
        ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
        ImGui::SameLine();
        ImGui::PushItemWidth(-100);
        ImGui::InputText("##spawn_search", props.spawnPropertiesSearch(), 128);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##spawn_reload_other")) {
            props.bindSpawnProperties(selectedClassFullName, true);
            char spawnPropsBuf[512];
            snprintf(spawnPropsBuf, sizeof(spawnPropsBuf), t("ui.spawn_table.reloaded_spawn"), selectedClassFullName.c_str());
            toastManager.addToastNotification(spawnPropsBuf, ToastTypeSuccess, 2.0);
        }

        std::string spFilter = toLowerStr(props.spawnPropertiesSearch());
        if (props.spawnProperties().empty()) {
            ImGui::TextDisabled(t("ui.spawn_table.class_no_edit"));
        } else {
            {
                ChildScope child("spawn_props_list", ImVec2(0, 220), true);
                if (child.open) {
                    props.renderPropertyTable(props.spawnPropertiesCDO(), nullptr, props.spawnProperties(), spFilter);
                }
            }
            ImGui::TextDisabled(t("ui.spawn_table.properties_applied"), (int)props.spawnProperties().size());
        }
    }
    ImGui::Unindent();
}

void UI::renderPackagesSection() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_BOX_ARCHIVE " ") + t("ui.game_pkg") + "##pkgs").c_str())) {
        return;
    }
    ImGui::Indent();

    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##pkg_search", packageSearch, sizeof(packageSearch));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT)) {
        lastPackageRefresh = ImGui::GetTime();
        collectPackagesAsync();
    }
    if (packagesCollecting.load()) {
        ImGui::SameLine();
        ImGui::TextDisabled(t("ui.game_pkg_table.scanning"));
    }

    std::string selectedName = (!packages.empty() && packageIndex >= 0 && packageIndex < (int)packages.size()) ? packages[packageIndex].name : "";

    ImGui::Text(t("ui.game_pkg_table.selected"));
    ImGui::SameLine();
    ImGui::Text("%s", selectedName.empty() ? t("ui.game_pkg_table.none") : selectedName.c_str());

    std::string filter = toLowerStr(packageSearch);
    {
        ChildScope child("pkg_list", ImVec2(0, 220), true);
        if (child.open) {
            if (filter.empty()) {
                ImGuiListClipper clipper;
                clipper.Begin((int)packages.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const PackageEntry& e = packages[i];
                        if (ImGui::Selectable((e.name + "##" + std::to_string(i)).c_str(), i == packageIndex)) {
                            packageIndex = i;
                        }
                    }
                }
                if (packages.empty()) {
                    ImGui::Text(t("ui.game_pkg_table.no_packages"));
                }
            } else {
                static std::vector<int> filteredPkg;
                filteredPkg.clear();
                for (int i = 0; i < (int)packages.size(); ++i) {
                    if (packages[i].nameLower.find(filter) != std::string::npos) {
                        filteredPkg.push_back(i);
                    }
                }
                if (filteredPkg.empty()) {
                    ImGui::Text(t("ui.game_pkg_table.no_packages"));
                } else {
                    ImGuiListClipper clipper;
                    clipper.Begin((int)filteredPkg.size());
                    while (clipper.Step()) {
                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                            int i = filteredPkg[n];
                            const PackageEntry& e = packages[i];
                            if (ImGui::Selectable((e.name + "##" + std::to_string(i)).c_str(), i == packageIndex)) {
                                packageIndex = i;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!selectedName.empty() && packageIndex >= 0 && packageIndex < (int)packages.size()) {
        const PackageEntry& sel = packages[packageIndex];
        ImGui::TextDisabled(t("ui.game_pkg_table.path"), sel.relPath.c_str());
        if (ImGui::Button((std::string(ICON_FA_DOWNLOAD " ") + t("ui.game_pkg_table.load")).c_str())) {
            const std::string loadPath = sel.path;
            const std::string loadName = sel.name;
            char loadingBuf[512];
            snprintf(loadingBuf, sizeof(loadingBuf), t("ui.game_pkg_table.loading"), loadName.c_str());
            toastManager.addToastNotification(loadingBuf, ToastTypeInfo, 2.0);
            Application::instance().engine().postPackageLoad(loadPath, [this, loadName]() {
                Application::instance().prefabs().markNeedsRefresh();
                char loadedBuf[512];
                snprintf(loadedBuf, sizeof(loadedBuf), t("ui.game_pkg_table.loaded"), loadName.c_str());
                toastManager.addToastNotification(loadedBuf, ToastTypeSuccess, 3.0);
            });
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_FA_EYE " ") + t("ui.game_pkg_table.preview")).c_str())) {
            packagePreviewName = sel.name;
            packagePreview = PCCFile();
            PCCParser parser(sel.path);
            if (parser.parse()) {
                packagePreview = parser.fileData();
                rebuildPackagePreviewCache();
                packagePreviewOpen = true;
            } else {
                char parseBuf[512];
                snprintf(parseBuf, sizeof(parseBuf), t("ui.game_pkg_table.failed_to_preview"), sel.name.c_str());
                toastManager.addToastNotification(parseBuf, ToastTypeError, 3.0);
            }
        }
    }

    if (packagePreviewOpen) {
        renderPackagePreview();
    }

    ImGui::Unindent();
}

void UI::rebuildPackagePreviewCache() {
    ZoneScopedN("UI::rebuildPackagePreviewCache");
    packagePreviewExportLabels.clear();
    packagePreviewExportLabelsLower.clear();
    packagePreviewImportLabels.clear();
    packagePreviewImportLabelsLower.clear();
    packagePreviewExportLabels.reserve(packagePreview.exports.size());
    packagePreviewExportLabelsLower.reserve(packagePreview.exports.size());
    for (const auto& ex : packagePreview.exports) {
        std::string label = ex.objectName;
        label += "  [export]";
        if (!ex.className.empty()) {
            label += "  [" + ex.className + "]";
        }
        if (ex.dataSize > 0) {
            label += "  (" + std::to_string(ex.dataSize) + " B)";
        }
        packagePreviewExportLabels.push_back(label);
        packagePreviewExportLabelsLower.push_back(toLowerStr(label));
    }
    packagePreviewImportLabels.reserve(packagePreview.imports.size());
    packagePreviewImportLabelsLower.reserve(packagePreview.imports.size());
    for (const auto& im : packagePreview.imports) {
        std::string label = im.objectName;
        label += "  [import]";
        if (!im.className.empty()) {
            label += "  [" + im.className + "]";
        }
        if (!im.packageName.empty()) {
            label += "  <" + im.packageName + ">";
        }
        packagePreviewImportLabels.push_back(label);
        packagePreviewImportLabelsLower.push_back(toLowerStr(label));
    }
}

bool UI::previewNodeMatches(int index, const std::string& filter) const {
    if (index < 0 || index >= (int)packagePreview.exports.size()) {
        return false;
    }
    if (filter.empty()) {
        return true;
    }
    if (index < (int)packagePreviewExportLabelsLower.size() && packagePreviewExportLabelsLower[index].find(filter) != std::string::npos) {
        return true;
    }
    const std::vector<int32_t>& kids = packagePreview.children[index];
    for (int32_t child : kids) {
        if (previewNodeMatches(child, filter)) {
            return true;
        }
    }
    return false;
}

void UI::renderPackagePreview() {
    ZoneScopedN("UI::renderPackagePreview");
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    char previewTitleBuf[512];
    snprintf(previewTitleBuf, sizeof(previewTitleBuf), t("ui.game_pkg_table.package_preview"), packagePreviewName.c_str());
    if (ImGui::Begin(previewTitleBuf, &packagePreviewOpen)) {
        ImGui::TextDisabled(t("ui.game_pkg_table.file"), packagePreviewName.c_str());
        ImGui::TextDisabled(t("ui.game_pkg_table.n_import_export"), packagePreview.exports.size(), packagePreview.imports.size());

        ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
        ImGui::SameLine();
        ImGui::PushItemWidth(-60);
        ImGui::InputText("##pkg_preview_search", packagePreviewSearch, sizeof(packagePreviewSearch));
        ImGui::PopItemWidth();
        ImGui::Separator();

        std::string filter = toLowerStr(packagePreviewSearch);
        // ensure cache is hot (in case preview was loaded before this optimization)
        if (packagePreviewExportLabels.size() != packagePreview.exports.size() || packagePreviewImportLabels.size() != packagePreview.imports.size()) {
            rebuildPackagePreviewCache();
        }

        if (ImGui::BeginChild("pkg_preview_tree", ImVec2(0, 0), true)) {
            if (packagePreview.rootExports.empty()) {
                ImGui::Text(t("ui.game_pkg_table.no_root"));
            } else {
                ImGui::Indent(4.0f);
                for (int32_t root : packagePreview.rootExports) {
                    // cull entire subtree if filter doesn't match
                    if (!filter.empty() && !previewNodeMatches(root, filter)) {
                        continue;
                    }
                    renderPackagePreviewNode(root, filter);
                }
                ImGui::Unindent(4.0f);
            }

            if (!packagePreview.imports.empty()) {
                ImGui::Separator();
                ImGui::Text(t("ui.game_pkg_table.imports"));
                ImGui::Indent(4.0f);
                int impShown = 0;
                for (size_t i = 0; i < packagePreviewImportLabels.size(); ++i) {
                    if (!filter.empty() && packagePreviewImportLabelsLower[i].find(filter) == std::string::npos) {
                        continue;
                    }
                    ImGui::BulletText("%s", packagePreviewImportLabels[i].c_str());
                    ++impShown;
                }
                if (impShown == 0) {
                    ImGui::Text(t("ui.game_pkg_table.no_imports_match"));
                }
                ImGui::Unindent(4.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// returns true if the node itself or any descendant matches the filter.
bool UI::renderPackagePreviewNode(int index, const std::string& filter) {
    ZoneScopedN("UI::renderPackagePreviewNode");
    if (index < 0 || index >= (int)packagePreview.exports.size()) {
        return false;
    }
    const std::string& label = (index < (int)packagePreviewExportLabels.size()) ? packagePreviewExportLabels[index] : packagePreview.exports[index].objectName;
    const std::vector<int32_t>& kids = packagePreview.children[index];

    if (kids.empty()) {
        ImGui::BulletText("%s", label.c_str());
        return true;
    }

    if (!filter.empty()) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    } else {
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    }
    if (ImGui::TreeNode((void*)(intptr_t)(index + 1), "%s", label.c_str())) {
        for (int32_t child : kids) {
            // only recurse into matching subtrees when filtering
            if (filter.empty() || previewNodeMatches(child, filter)) {
                renderPackagePreviewNode(child, filter);
            }
        }
        ImGui::TreePop();
    }
    return true;
}
#pragma endregion
