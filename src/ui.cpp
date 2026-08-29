#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "ui.h"
#include "ui_helpers/raii_guards.h"
#include "ui_helpers/toast_notifications.h"
#include <LESDK/Includes.LE2.hpp>
#include <algorithm>
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
#include "props.h"

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
    std::string sel = (!pawnNamesVector.empty() && pawnIndexInt >= 0 && pawnIndexInt < (int)pawnNamesVector.size()) ? pawnNamesVector[pawnIndexInt] : "";
    pawnNamesVector.clear();
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
    if (!advancedSelection) {
        advancedSelection = true;
        collectPawns();
    }

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
        index = (int)pawnNamesVector.size() - 1;
    }
    pawnIndexInt = index;
    if (std::find(pinnedNamesVector.begin(), pinnedNamesVector.end(), nm) == pinnedNamesVector.end()) {
        pinnedNamesVector.push_back(nm);
    }
    Application::instance().gizmo().setTarget(actor);
}

void UI::collectClasses() {
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
    ImGui::Text("Position");
    ImGui::PushItemWidth(-100);
    edited |= axisWidgetLambda("X", "##px", &t.pos[0], 1.0f, -100000.f, 100000.f, "%.1f");
    edited |= axisWidgetLambda("Y", "##py", &t.pos[1], 1.0f, -100000.f, 100000.f, "%.1f");
    edited |= axisWidgetLambda("Z", "##pz", &t.pos[2], 1.0f, -100000.f, 100000.f, "%.1f");
    ImGui::Text("Rotation");
    edited |= axisWidgetLambda("RX", "##rx", &t.rot[0], 0.1f, -180.f, 180.f, "%.1f");
    edited |= axisWidgetLambda("RY", "##ry", &t.rot[1], 0.1f, -180.f, 180.f, "%.1f");
    edited |= axisWidgetLambda("RZ", "##rz", &t.rot[2], 0.1f, -180.f, 180.f, "%.1f");
    ImGui::Text("Scale");
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
        Application::instance().engine().isCameraDragActive() = false;
        ClipCursor(NULL);
        ReleaseCapture();
    }
}

// main function to render the overlay contents
void UI::renderOverlayContents(NativeRenderer& renderer) {
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

    ImGui::Begin(ICON_FA_SLIDERS " SuperAdjustmentStudio " PLUGIN_VERSION, NULL);
    ImVec2 windowPosition = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    renderer.setUiRect({(LONG)windowPosition.x, (LONG)windowPosition.y, (LONG)(windowPosition.x + windowSize.x), (LONG)(windowPosition.y + windowSize.y)});

    if (Application::instance().animation().animPauseActive()) {
        Application::instance().animation().keepAnimationsPaused();
    }
    // DO NOT run Bones:keepBonePoses here -> SHOULD ALWAYS RUN ON GAME THREAD ABioHUD::PostRender

    renderControlsSection();
    renderSelectionSection();
    renderSpawnSection();

    ImGui::End();
    toastManager.renderToastNotifications();
    Application::instance().properties().renderStructWindows();
}

#pragma region //  CONTROLS
void UI::renderControlsSection() {
    ImGui::Text("Controls");

    ImGui::BeginTable("primary_controls##primary_controls", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    const char* icon;

    icon = pauseTime ? ICON_FA_PAUSE : ICON_FA_PLAY;
    if (ImGui::Checkbox((std::string(icon) + " Pause").c_str(), &pauseTime)) {
        Application::instance().engine().setPause(pauseTime);
        Application::instance().animation().pauseAnimations(pauseTime);
    }

    ImGui::TableNextColumn();
    // i have a theory, it doesn't work here, cause it should be run on PostRender, instead of Present...
    bool hideGameUI = Application::instance().engine().isGameUIHidden();
    icon = hideGameUI ? ICON_FA_EYE_SLASH : ICON_FA_EYE;
    if (ImGui::Checkbox((std::string(icon) + " Hide all game UI").c_str(), &hideGameUI)) {
        Application::instance().engine().isGameUIHidden() = hideGameUI;
        Application::instance().engine().applyHUDVisibility();
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = Application::instance().gizmo().clickSelect() ? ICON_FA_ARROW_POINTER : ICON_FA_HAND_POINTER;
    ImGui::Checkbox((std::string(icon) + " Click to select").c_str(), &Application::instance().gizmo().clickSelect());
    ImGui::TableNextColumn();

    icon = advancedSelection ? ICON_FA_HAND : ICON_FA_HAND_BACK_FIST;
    if (ImGui::Checkbox((std::string(icon) + " Advanced selection").c_str(), &advancedSelection)) {
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
        if (ImGui::Checkbox((std::string(icon) + " Float selected pawn (ignore collisions)").c_str(), &floatEnabled)) {
            Application::instance().engine().setFloat(floatPawn, floatEnabled);
        }
    }

    ImGui::TableNextColumn();
    icon = ICON_FA_TRIANGLE_EXCLAMATION;
    if (ImGui::Checkbox((std::string(icon) + " Allow array slack expansion ").c_str(), &Application::instance().properties().allowArraySlackExpansion)) {
        if (Application::instance().properties().allowArraySlackExpansion) {
            toastManager.addToastNotification("Enabling this option can be dangerous! It allows adding new elements to arrays, which requires reallocation.",
                                              ToastTypeWarning, 3.0);
        }
    }

    ImGui::EndTable();

    ImGui::Separator();

    ImGui::BeginTable("gizmos_table##gizmos_table", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = ICON_FA_CUBE;
    ImGui::Checkbox((std::string(icon) + " Show orientation gizmo").c_str(), &Application::instance().gizmo().showGizmo());
    ImGui::TableNextColumn();
    icon = ICON_FA_LAYER_GROUP;
    ImGui::Checkbox((std::string(icon) + " Draw gizmos always on top").c_str(), &Application::instance().gizmo().debugAlwaysOnTop());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    icon = ICON_FA_ARROWS_TO_DOT;
    ImGui::Checkbox((std::string(icon) + " Draw tracer").c_str(), &Application::instance().gizmo().drawTracer());
    ImGui::TableNextColumn();
    icon = ICON_FA_HIGHLIGHTER;
    ImGui::Checkbox((std::string(icon) + " Highlight selected").c_str(), &Application::instance().gizmo().highlightSelected());
    ImGui::EndTable();
    ImGui::Separator();
}
#pragma endregion

#pragma region //  SELECTION
void UI::renderSelectionSection() {
    if (!ImGui::CollapsingHeader(ICON_FA_OBJECT_GROUP " Selection")) {
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
    if (!ImGui::CollapsingHeader(ICON_FA_CROSSHAIRS " Target##sel")) {
        return;
    }
    ImGui::Indent();

    if (!pawnNamesVector.empty() && (pawnIndexInt < 0 || pawnIndexInt >= (int)pawnNamesVector.size())) {
        pawnIndexInt = 0;
    }
    const char* pawnPreview = (pawnNamesVector.empty() || pawnIndexInt < 0 || pawnIndexInt >= (int)pawnNamesVector.size())
                                  ? "(no objects found)"
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

    {
        ChildScope child("sel_list", ImVec2(0, 120), true);
        ImGui::PushItemWidth(-100);
        if (child.open) {
            for (int i = 0; i < (int)pawnNamesVector.size(); ++i) {
                std::string id = pawnNamesVector[i] + "##" + std::to_string(i);
                std::string filterLower = toLowerStr(targetSearchFilter);
                std::string pawnLower = toLowerStr(pawnNamesVector[i]);
                if (filterLower.empty() || pawnLower.find(filterLower) != std::string::npos) {
                    if (ImGui::Selectable(id.c_str(), i == pawnIndexInt)) {
                        pawnIndexInt = i;
                        Application::instance().gizmo().clearExplicitTarget();
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
    if (!ImGui::CollapsingHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform##sel")) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled("Applies live to the selected object every 250ms.");
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

    if (ImGui::Button(ICON_FA_ARROW_RIGHT_TO_BRACKET " Reload from selected##sel")) {
        if (target) {
            transformPawn = FStringToUtf8(target->GetName());
            Application::instance().engine().loadTransformFromActor(target, selectedTransform);
            transformToApply = false;
            toastManager.addToastNotification("Reloaded transform from " + transformPawn, ToastTypeSuccess, 2.0);
        }
    }

    bool tfEdited = renderTransformEditor(selectedTransform, "sel");
    if (tfEdited) {
        transformToApply = true;
        transformLastApply = ImGui::GetTime();
    }
    if (transformToApply && target) {
        applyDebounced(transformToApply, transformLastEdit, transformLastApply, [&] {
            Application::instance().engine().setTransform(target, selectedTransform);
        });
    }
    ImGui::Unindent();
}

void UI::renderSelectionAnimation() {
    if (!ImGui::CollapsingHeader(ICON_FA_FILM " Animation")) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled("Applies live to the selected object, the object must have bones (eg. a Pawn).");

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
            for (int i = 0; i < (int)animationNames.size(); ++i) {
                if (!aFilter.empty() && toLowerStr(animationNames[i]).find(aFilter) == std::string::npos) {
                    continue;
                }
                ++aShown;
                std::string id = animationNames[i] + "##" + std::to_string(i);
                if (ImGui::Selectable(id.c_str(), i == animationIndex)) {
                    animationIndex = i;
                }
            }
            if (aShown == 0) {
                ImGui::Text("(no matches)");
            }
        }
    }

    ImGui::Text(ICON_FA_CROSSHAIRS " Selected animation: ");
    ImGui::SameLine();
    ImGui::Text("%s", animationNames.empty() ? "(none)" : animationNames[animationIndex].c_str());

    ImGui::Separator();
    bool canPlay = !pawnNamesVector.empty() && !animationNames.empty();
    if (ImGui::Button(ICON_FA_PLAY " Play") && canPlay) {
        Application::instance().animation().playAnimation(pawnNamesVector[pawnIndexInt], animationNames[animationIndex], false);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_REPEAT " Play (loop)") && canPlay) {
        Application::instance().animation().playAnimation(pawnNamesVector[pawnIndexInt], animationNames[animationIndex], true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Reset") && !pawnNamesVector.empty()) {
        Application::instance().animation().resetAnimation(pawnNamesVector[pawnIndexInt]);
    }

    if (ImGui::Checkbox("Include all loaded animation sets", &animationIncludeAll)) {
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
    if (!ImGui::CollapsingHeader(ICON_FA_BONE " Bones")) {
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

    const char* meshTargets[2] = {"Body", "Head"};
    int oldTarget = meshTargetIndex;
    ImGui::Text("Target mesh:");

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
        ImGui::TextDisabled("(no bones found in the mesh)");
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
            for (int i = 0; i < (int)bones.size(); ++i) {
                const BonePoseInfo& b = bones[i];
                std::string label = b.boneName;
                if (label.empty()) {
                    label = "(bone " + std::to_string(b.index) + ")";
                }
                if (!b.parentName.empty()) {
                    label += " < " + b.parentName;
                }
                if (!bFilter.empty() && toLowerStr(label).find(bFilter) == std::string::npos) {
                    continue;
                }
                if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), i == boneIndex)) {
                    boneIndex = i;
                    BonePoseInfo fresh;
                    if (Application::instance().bones().getBoneTransform(pawn, (MeshTarget)meshTargetIndex, b.index, fresh)) {
                        boneEdit = fresh;
                        boneEdit.pos[0] = boneEdit.pos[1] = boneEdit.pos[2] = 0.0f; // position is an offset from current pose
                        boneToApply = false;
                    }
                }
                ++bShown;
            }
            if (bShown == 0) {
                ImGui::Text("(no bones match)");
            } else {
                ImGui::Text("%d bone(s)", (int)bones.size());
            }
        }
    }
    ImGui::Separator();

    const BonePoseInfo& sel = bones[boneIndex];
    ImGui::Text("Selected bone: %s", sel.boneName.c_str());
    if (!sel.parentName.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(parent: %s)", sel.parentName.c_str());
    }
    ImGui::Separator();
    ImGui::PushItemWidth(-100);
    ImGui::Text("Position offset");
    bool bEdited = axisWidgetLambda("X", "##dir_px", &boneEdit.pos[0], 0.5f, -10000.0f, 10000.0f, "%.2f");
    bEdited |= axisWidgetLambda("Y", "##dir_py", &boneEdit.pos[1], 0.5f, -10000.0f, 10000.0f, "%.2f");
    bEdited |= axisWidgetLambda("Z", "##dir_pz", &boneEdit.pos[2], 0.5f, -10000.0f, 10000.0f, "%.2f");
    ImGui::Text("Rotation");
    bEdited |= axisWidgetLambda("RX", "##dir_rx", &boneEdit.rot[0], 0.5f, -360.0f, 360.0f, "%.2f");
    bEdited |= axisWidgetLambda("RY", "##dir_ry", &boneEdit.rot[1], 0.5f, -360.0f, 360.0f, "%.2f");
    bEdited |= axisWidgetLambda("RZ", "##dir_rz", &boneEdit.rot[2], 0.5f, -360.0f, 360.0f, "%.2f");
    ImGui::Text("Scale"); // FBoneAtom has no per-axis scale
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
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Reload from bone##bones")) {
        BonePoseInfo fresh;
        if (Application::instance().bones().getBoneTransform(pawn, (MeshTarget)meshTargetIndex, sel.index, fresh)) {
            boneEdit = fresh;
            boneEdit.pos[0] = boneEdit.pos[1] = boneEdit.pos[2] = 0.0f;
            boneToApply = false;
            toastManager.addToastNotification("Reloaded bone pose from " + sel.boneName, ToastTypeSuccess, 2.0);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Reset pose##bones_dir")) {
        Application::instance().bones().resetBonePose(pawn, (MeshTarget)meshTargetIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_LAND_MINE_ON " Absolute reset##bones")) {
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
    if (!ImGui::CollapsingHeader(ICON_FA_LIST " Other properties##sel")) {
        return;
    }
    ImGui::Indent();
    ImGui::TextDisabled("Applies live, upon edit.");

    Properties& props = Application::instance().properties();
    if (!pawnNamesVector.empty()) {
        AActor* propActor = Application::instance().engine().findActorByName(props.propertyPawn());
        if (props.propertyPawn() != pawnNamesVector[pawnIndexInt]) {
            props.propertyPawn() = pawnNamesVector[pawnIndexInt];
            props.propertyObject() = nullptr;
            props.propertyComponentIndex() = 0;
        }
        if (!propActor) {
            ImGui::Text("(target not found)");
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

            ImGui::Text("Target component");
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

            if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Reload properties")) {
                props.propertyObject() = target;
                props.collectProperties(target, props.properties());
                toastManager.addToastNotification("Reloaded properties for " + FStringToUtf8(target->GetName()), ToastTypeSuccess, 2.0);
            }

            std::string pFilter = toLowerStr(props.propertySearch());
            ImGui::Text("%d properties on %s", (int)props.properties().size(), FStringToUtf8(target->GetName()).c_str());
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
    if (!ImGui::CollapsingHeader(ICON_FA_CUBES_STACKED " Spawn")) {
        return;
    }
    ImGui::Indent();
    ImColor redColor = ImColor(1.0f, 0.4f, 0.4, 1.0f);

    if (!UObject::GObjObjects) {
        ImGui::TextColored(redColor, "SDK globals not initialized!!! Check log file for more info.");
    } else {
        renderSpawnClassList();
        renderSpawnTransform();
        Application::instance().particles().renderUI();
        renderSpawnOtherProps();
    }
    ImGui::Unindent();
}

void UI::renderSpawnClassList() {
    // simplification for the sake of the user
    if (!ImGui::CollapsingHeader(ICON_FA_CUBES " Object list##spawn")) {
        return;
    }
    ImGui::Indent();
    ImColor orangeColor = ImColor(1.0f, 0.6f, 0.2f, 1.0f);

    ImGui::TextColored(orangeColor, ICON_FA_TRIANGLE_EXCLAMATION " WARNING: Spawning an invalid actor can and will cause a crash!");

    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    ImGui::InputText("##class_search", classSearch, sizeof(classSearch));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT)) {
        collectClasses();
    }

    ImGui::Text("Selected class:");
    ImGui::SameLine();
    ImGui::Text("%s", selectedClassFullName.empty() ? "(none)" : selectedClassFullName.c_str());

    std::string filter = toLowerStr(classSearch);
    {
        ChildScope child("class_list", ImVec2(0, 220), true);
        if (child.open) {
            std::string lastPkg;
            int shown = 0;
            for (int i = 0; i < (int)classes.size(); ++i) {
                const ClassEntry& e = classes[i];
                if (!filter.empty()) {
                    std::string fullLower = toLowerStr(e.fullName);
                    std::string pkgLower = toLowerStr(e.package);
                    if (fullLower.find(filter) == std::string::npos && pkgLower.find(filter) == std::string::npos) {
                        continue;
                    }
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
                ImGui::Text("(no classes match)");
            }
        }
    }

    if (!selectedClassFullName.empty()) {
        // keep it bound
        Properties& props = Application::instance().properties();
        props.bindSpawnProperties(selectedClassFullName, false);
        if (ImGui::Button(ICON_FA_PLUS " Spawn selected")) {
            AActor* spawned = Application::instance().engine().spawnClass(selectedClassFullName, spawnTransform);
            // auto-select spawned
            if (spawned) {
                props.applySpawnProperties(spawned, props.spawnProperties());
                std::string nm = FStringToUtf8(spawned->GetName());
                collectPawns();
                for (int i = 0; i < (int)pawnNamesVector.size(); ++i) {
                    if (pawnNamesVector[i] == nm) {
                        pawnIndexInt = i;
                        toastManager.addToastNotification("Spawned " + selectedClassFullName + " as " + nm, ToastTypeSuccess, 2.0);
                        break;
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STETHOSCOPE " Diagnose class")) {
            toastManager.addToastNotification("Diagnosing " + selectedClassFullName + " (check log file)", ToastTypeInfo, 2.0);
            Logger->debug(Application::instance().engine().diagnoseClass(selectedClassFullName));
        }
    }
    ImGui::Unindent();
}

void UI::renderSpawnTransform() {
    if (!ImGui::CollapsingHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform##spawn")) {
        return;
    }
    ImGui::Indent();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT_TO_BRACKET " Reload from selected##spawn")) {
        if (!pawnNamesVector.empty()) {
            Application::instance().engine().loadTransformFromPawn(pawnNamesVector[pawnIndexInt], spawnTransform);
            toastManager.addToastNotification("Reloaded transform from " + pawnNamesVector[pawnIndexInt], ToastTypeSuccess, 2.0);
        }
    }
    renderTransformEditor(spawnTransform, "spawn");
    ImGui::Unindent();
}

void UI::renderSpawnOtherProps() {
    if (!ImGui::CollapsingHeader(ICON_FA_LIST " Other properties##spawn")) {
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
            toastManager.addToastNotification("Reloaded spawn properties for " + selectedClassFullName, ToastTypeSuccess, 2.0);
        }

        std::string spFilter = toLowerStr(props.spawnPropertiesSearch());
        if (props.spawnProperties().empty()) {
            ImGui::TextDisabled("(class not loaded or no editable properties)");
        } else {
            {
                ChildScope child("spawn_props_list", ImVec2(0, 220), true);
                if (child.open) {
                    props.renderPropertyTable(props.spawnPropertiesCDO(), nullptr, props.spawnProperties(), spFilter);
                }
            }
            ImGui::TextDisabled("%d properties (applied upon Spawn)", (int)props.spawnProperties().size());
        }
    }
    ImGui::Unindent();
}
#pragma endregion
