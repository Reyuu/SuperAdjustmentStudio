#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"
#include "IconsFontAwesome6.h"
#include "application.h"
#include "imgui.h"
#include "logger.h"
#include "ui.h"
#include "ui/helpers/raii_guards.h"
#include "ui/helpers/toast_notifications.h"
#include "util.h"

#include "vfx.h"
#include <LESDK/Includes.hpp>

#include "tracy.h"

#ifdef SDK_TARGET_LE3
#include "le3_compat.h"
#endif

VfxTemplateT* VFXManager::findTemplateByName(const std::string& name) {
    VfxTemplateT* found = nullptr;
    forEachOf<VfxTemplateT>([&](VfxTemplateT* vfx) {
        if (vfx && toLowerStr(FStringToUtf8(vfx->GetName())).find(toLowerStr(name)) != std::string::npos) {
            found = vfx;
        }
    });
    return found;
}

#ifdef SDK_TARGET_LE3
void VFXManager::addVFX(VfxTemplateT* vfxTemplate, AActor* actor, const std::string& boneName, float lifeTime, double spawnTime) {
    if (!vfxTemplate) {
        Logger->debug("addVFX: vfxTemplate is null");
        return;
    }
    if (!actor) {
        Logger->debug("addVFX: actor is null");
        return;
    }
    URvrClientEffectManager* mgr = GetCEManager();
    if (!mgr) {
        Logger->debug("addVFX: no CE manager available");
        return;
    }
    FRvrClientEffectTarget target{};
    target.HitActor = actor;
    if (!boneName.empty()) {
        target.HitBone = SFXName(boneName.c_str(), 0);
    }
    FGuid guid = mgr->StartOnTarget(vfxTemplate, actor, &target);
    if (IsGuidZero(guid)) {
        Logger->debug("addVFX: StartOnTarget failed for '{}'", FStringToUtf8(vfxTemplate->GetName()));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(vfxMtx);
        VFXEntry entry;
        entry.name = FStringToUtf8(vfxTemplate->GetName());
        entry.path = FStringToUtf8(vfxTemplate->GetFullPath());
        entry.pawnName = FStringToUtf8(actor->GetName());
        entry.boneName = boneName;
        entry.lifeTime = lifeTime;
        entry.spawnTime = spawnTime;
        entry.templateRef = vfxTemplate;
        entry.effectGuid = guid;
        entry.effectOwner = actor;
        entry.loop = loopVFX;
        entry.loopDelay = loopDelayVFX;
        vfxEntries.push_back(entry);
    }
    applyVFXLiveState(vfxEntries.back());
}
#else
void VFXManager::addVFX(VfxTemplateT* vfxTemplate, AActor* actor, const std::string& boneName, float lifeTime, double spawnTime) {
    if (!vfxTemplate) {
        Logger->debug("addVFX: vfxTemplate is null");
        return;
    }
    if (!actor) {
        Logger->debug("addVFX: actor is null");
        return;
    }
    if (boneName.empty()) {
        Logger->debug("addVFX: boneName is empty");
        return;
    }
    if (!vfxTemplate->bIsInitialized) {
        Logger->debug("addVFX: vfxTemplate is not initialized");
        return;
    }

    ABioVisualEffect* donor = findFirstOf<ABioVisualEffect>();
    if (!donor) {
        ABioWorldInfo* worldInfo = findFirstOf<ABioWorldInfo>();
        if (worldInfo) {
            UBioVisualEffectPool* vfxPool = worldInfo->GetVFXPool(vfxTemplate);
            if (vfxPool) {
                donor = vfxPool->GetEffect();
            } else {
                Logger->debug("addVFX: no VFX pool for template '{}'", FStringToUtf8(vfxTemplate->GetName()));
            }
        } else {
            Logger->debug("addVFX: no world info found");
        }
    }

    if (!isLiveObject(donor)) {
        Logger->debug("addVFX: no donor effect available");
        return;
    }
    ABioVisualEffect* vfxActor = nullptr;
    vfxActor = donor->CreateCrustEffect(vfxTemplate, actor, lifeTime, 0);
    if (!vfxActor && actor->IsA(APawn::StaticClass())) {
        vfxActor = donor->CreateVFXOnMesh(vfxTemplate, actor, SFXName(boneName.c_str(), 0), lifeTime, static_cast<APawn*>(actor)->Mesh, 0);
    }
    if (!vfxActor && actor->IsA(ASFXPawn::StaticClass())) {
        ASFXPawn* sfxPawn = static_cast<ASFXPawn*>(actor);
        sfxPawn->CreateVisualEffect(vfxTemplate, &vfxActor);
    }

    if (vfxActor) {
        // capture originals before camera stripping, for later restore
        {
            std::lock_guard<std::mutex> lock(vfxMtx);
            VFXEntry entry;
            entry.name = FStringToUtf8(vfxTemplate->GetName());
            entry.path = FStringToUtf8(vfxTemplate->GetFullPath());
            entry.pawnName = FStringToUtf8(actor->GetName());
            entry.boneName = boneName;
            entry.lifeTime = lifeTime;
            entry.spawnTime = spawnTime;
            entry.actor = vfxActor;
            entry.loop = loopVFX;
            entry.loopDelay = loopDelayVFX;
            entry.cameraShake = vfxActor->m_cameraShake;
            entry.cameraShakenActor = vfxActor->m_cameraShakenActor;
            vfxEntries.push_back(entry);
        }
        applyVFXLiveState(vfxEntries.back());
    }
}
#endif

static void eraseEntry(std::vector<VFXEntry>& entries, VFXEntry& entry) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (&(*it) == &entry) {
            entries.erase(it);
            return;
        }
    }
}

void VFXManager::removeVFX(VFXEntry& entry) {
    std::lock_guard<std::mutex> lock(vfxMtx);
#ifdef SDK_TARGET_LE3
    if (entry.templateRef && !IsGuidZero(entry.effectGuid) && entry.effectOwner) {
        if (URvrClientEffectManager* mgr = GetCEManager()) {
            mgr->Stop(entry.templateRef, entry.effectGuid, 0, entry.effectOwner);
        }
    }
#else
    if (entry.actor) {
        entry.actor->SetPaused(1, true);
        entry.actor->SetLifeTime(0.0f);
        entry.actor->LoopDuration(0);
        entry.actor->PauseOnDeath(1);
        entry.actor->fStateDurations[0] = 0.0f; // SPAWN
        entry.actor->fStateDurations[1] = 0.0f; // LIFE
        entry.actor->bActive = 0;
        entry.actor->bPaused = 1;
        entry.actor->SetState(2, true, true, false); // BVFX_DEATH
        entry.actor->eventOnComplete();
        entry.actor = nullptr;
    }
#endif
    eraseEntry(vfxEntries, entry);
}

void VFXManager::removeAllVFX() {
    std::lock_guard<std::mutex> lock(vfxMtx);
#ifdef SDK_TARGET_LE3
    URvrClientEffectManager* mgr = GetCEManager();
    for (VFXEntry& e : vfxEntries) {
        if (mgr && e.templateRef && !IsGuidZero(e.effectGuid) && e.effectOwner) {
            mgr->Stop(e.templateRef, e.effectGuid, 0, e.effectOwner);
        }
    }
#else
    for (VFXEntry& e : vfxEntries) {
        if (e.actor && isLiveObject(e.actor)) {
            e.actor->SetPaused(1, true);
            e.actor->SetLifeTime(0.0f);
            e.actor->LoopDuration(0);
            e.actor->PauseOnDeath(1);
            e.actor->fStateDurations[0] = 0.0f; // SPAWN
            e.actor->fStateDurations[1] = 0.0f; // LIFE
            e.actor->bActive = 0;
            e.actor->bPaused = 1;
            e.actor->SetState(2, true, true, false); // BVFX_DEATH
            e.actor->eventOnComplete();
        }
        e.actor = nullptr;
    }
#endif
    vfxEntries.clear();
}

void VFXManager::applyVFXLiveState(VFXEntry& entry) {
#ifdef SDK_TARGET_LE3
    (void)entry; // manager owns lifetime and looping in LE3
#else
    ABioVisualEffect* a = entry.actor;
    if (!a) {
        return;
    }
    if (ignoreCameraMovement) {
        a->m_cameraShake = nullptr;
        a->m_cameraShakenActor = nullptr;
    } else {
        a->m_cameraShake = entry.cameraShake;
        a->m_cameraShakenActor = entry.cameraShakenActor;
    }
    a->SetPaused(0, true);
    a->SetLifeTime(entry.lifeTime);
    a->LoopDuration(entry.loop ? 1 : 0);
    a->PauseOnDeath(0);
    a->fStateDurations[0] = entry.lifeTime; // SPAWN
    a->fStateDurations[1] = entry.lifeTime; // LIFE
    a->bActive = 1;
    a->bPaused = 0;
    a->bDeleteSelf = 0; // keep the revived effect alive past re-trigger
    // eCurrentState: 0 SPAWN, 1 LIFE, 2 DEATH
    a->SetState(1, true, true, false); // BVFX_LIFE
#endif
}

#ifdef SDK_TARGET_LE3
void VFXManager::updateActiveVFX() {
    std::lock_guard<std::mutex> lock(vfxMtx);
    double now = ImGui::GetTime();
    for (auto it = vfxEntries.begin(); it != vfxEntries.end();) {
        VFXEntry& e = *it;
        if (e.loop) {
            if (e.nextLoopTime == 0.0) {
                e.nextLoopTime = now + e.loopDelay;
            } else if (now >= e.nextLoopTime) {
                if (URvrClientEffectManager* mgr = GetCEManager(); mgr && e.templateRef && e.effectOwner) {
                    mgr->Stop(e.templateRef, e.effectGuid, 0, e.effectOwner);
                    FRvrClientEffectTarget target{};
                    target.HitActor = e.effectOwner;
                    if (!e.boneName.empty()) {
                        target.HitBone = SFXName(e.boneName.c_str(), 0);
                    }
                    e.effectGuid = mgr->StartOnTarget(e.templateRef, e.effectOwner, &target);
                }
                e.nextLoopTime = 0.0;
            }
            ++it;
        } else if ((now - e.spawnTime) >= (double)e.lifeTime) {
            it = vfxEntries.erase(it);
        } else {
            ++it;
        }
    }
}
#else
void VFXManager::updateActiveVFX() {
    std::lock_guard<std::mutex> lock(vfxMtx);
    double now = ImGui::GetTime();
    for (auto it = vfxEntries.begin(); it != vfxEntries.end();) {
        VFXEntry& e = *it;
        if (!isLiveObject(e.actor)) {
            it = vfxEntries.erase(it);
            continue;
        }
        if (e.loop) {
            bool ended = !e.actor->bActive || e.actor->bDeleteSelf || e.actor->eCurrentState == 2;
            if (ended) {
                if (e.nextLoopTime == 0.0) {
                    e.nextLoopTime = now + e.loopDelay;
                } else if (now >= e.nextLoopTime) {
                    applyVFXLiveState(e);
                    e.nextLoopTime = 0.0;
                }
            }
            ++it;
        } else if ((now - e.spawnTime) >= (double)e.lifeTime) {
            it = vfxEntries.erase(it);
        } else {
            ++it;
        }
    }
}
#endif

bool VFXTemplateNameLess::operator()(VfxTemplateT* a, VfxTemplateT* b) const {
    return toLowerStr(FStringToUtf8(a->GetName())) < toLowerStr(FStringToUtf8(b->GetName()));
}

void VFXManager::findAvailableTemplates(bool forceRefresh) {
    std::lock_guard<std::mutex> lock(vfxMtx);
    if (!forceRefresh && !availableTemplates.empty()) {
        return;
    }
    availableTemplates.clear();
    forEachOf<VfxTemplateT>([&](VfxTemplateT* vfx) {
        if (!vfx) {
            return;
        }
#ifdef SDK_TARGET_LE3
        availableTemplates.insert(vfx);
#else
        if (vfx->bIsInitialized)
            availableTemplates.insert(vfx);
#endif
    });
}

void VFXManager::renderUI() {
    ZoneScopedN("VFX::renderUI");
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_FIRE " ") + t("ui.vfx")).c_str())) {
        return;
    }

    static std::string selectedVFXName;
    findAvailableTemplates(); // populate on first use
    {
        static float lastAutoPrune = 0.0f;
        if (ImGui::GetTime() - lastAutoPrune > 0.25f) {
            lastAutoPrune = ImGui::GetTime();
            Application::instance().engine().postGameThreadTask([this]() {
                updateActiveVFX();
            });
        }
    }

    ImGui::Indent();

    static std::string boneSelect;
    static std::string cachedPawnForBones;
    static std::vector<BonePoseInfo> cachedBones;
    {
        std::string curPawn = Application::instance().ui().getSelectedPawnName();
        if (curPawn != cachedPawnForBones) {
            cachedPawnForBones = curPawn;
            cachedBones.clear();
            if (!curPawn.empty()) {
                Application::instance().bones().listBones(curPawn, MESH_BODY, cachedBones);
            }
        }
    }

    std::vector<BonePoseInfo>& bones = cachedBones;
    if (showBoneSelection) {
        if (boneSelect.empty() && !bones.empty()) {
            boneSelect = bones[0].boneName;
        }
        ImGui::Text(t("ui.vfx_table.bone"));
        ImGui::PushItemWidth(-100);
        if (ImGui::BeginCombo("##vfx_bone", boneSelect.c_str())) {
            for (const BonePoseInfo& b : bones) {
                if (ImGui::Selectable(b.boneName.c_str())) {
                    boneSelect = b.boneName;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
    } else {
        boneSelect = bones.empty() ? "" : bones[0].boneName;
    }

    ImGui::Separator();
    ImGui::Text(t("ui.vfx_table.available_vfx"));
    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    static char vfxSearchFilter[256] = "";
    ImGui::InputText("##class_search", vfxSearchFilter, sizeof(vfxSearchFilter));
    std::string filterLower = toLowerStr(vfxSearchFilter);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##vfx_refresh")) {
        findAvailableTemplates(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##vfx_spawn_btn")) {
        AActor* actor = Application::instance().engine().findActorByName(Application::instance().ui().getSelectedPawnName());
        if (actor) {
            VfxTemplateT* tmpl = VFXManager::findTemplateByName(selectedVFXName);
            if (tmpl) {
                double spawnTime = ImGui::GetTime();
                Application::instance().engine().postGameThreadTask([this, tmpl, actor, boneSelect = boneSelect, spawnTime]() {
                    addVFX(tmpl, actor, boneSelect.c_str(), vfxDuration, spawnTime);
                });
            }
        }
    }

    {
        ChildScope child("##vfx_available_list", ImVec2(0, 120), true);
        if (child.open) {
            static std::vector<std::string> filteredNames;
            filteredNames.clear();
            if (filterLower.empty()) {
                filteredNames.reserve(availableTemplates.size());
                for (auto* tmpl : availableTemplates) {
                    if (!tmpl) {
                        continue;
                    }
                    filteredNames.push_back(FStringToUtf8(tmpl->GetName()));
                }
                ImGuiListClipper clipper;
                clipper.Begin((int)filteredNames.size());
                while (clipper.Step()) {
                    for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                        const std::string& name = filteredNames[n];
                        std::string id = name + "##" + std::to_string(n);
                        if (ImGui::Selectable(id.c_str(), selectedVFXName == name)) {
                            selectedVFXName = name;
                        }
                    }
                }
            } else {
                for (auto* tmpl : availableTemplates) {
                    if (!tmpl) {
                        continue;
                    }
                    std::string name = FStringToUtf8(tmpl->GetName());
                    if (toLowerStr(name).find(filterLower) == std::string::npos) {
                        continue;
                    }
                    filteredNames.push_back(name);
                }
                if (filteredNames.empty()) {
                    ImGui::TextDisabled(t("ui.vfx_table.no_matches"));
                } else {
                    ImGuiListClipper clipper;
                    clipper.Begin((int)filteredNames.size());
                    while (clipper.Step()) {
                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                            const std::string& name = filteredNames[n];
                            std::string id = name + "##f" + std::to_string(n);
                            if (ImGui::Selectable(id.c_str(), selectedVFXName == name)) {
                                selectedVFXName = name;
                            }
                        }
                    }
                }
            }
        }
    }

    ImGui::Separator();

    ImGui::BeginTable("##vfx_table", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::Checkbox((std::string(ICON_FA_CAMERA " ") + t("ui.vfx_table.ignore_cam_movement") + "##vfx_ignore_cam").c_str(), &ignoreCameraMovement)) {
        Application::instance().engine().postGameThreadTask([this]() {
            std::lock_guard<std::mutex> lock(vfxMtx);
#ifndef SDK_TARGET_LE3
            for (VFXEntry& e : vfxEntries) {
                if (!e.actor) {
                    continue;
                }
                if (ignoreCameraMovement) {
                    e.actor->m_cameraShake = nullptr;
                    e.actor->m_cameraShakenActor = nullptr;
                } else {
                    e.actor->m_cameraShake = e.cameraShake;
                    e.actor->m_cameraShakenActor = e.cameraShakenActor;
                }
            }
#endif
        });
    }
    ImGui::TableNextColumn();

    if (ImGui::Checkbox((std::string(ICON_FA_REPEAT " ") + t("ui.vfx_table.loop") + "##vfx_loop").c_str(), &loopVFX)) {
        Application::instance().engine().postGameThreadTask([this]() {
            std::lock_guard<std::mutex> lock(vfxMtx);
            for (VFXEntry& e : vfxEntries) {
                e.loop = loopVFX;
                e.loopDelay = loopDelayVFX;
                e.nextLoopTime = 0.0;
#ifndef SDK_TARGET_LE3
                if (e.actor)
                    e.actor->LoopDuration(loopVFX ? 1 : 0);
#endif
            }
        });
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Checkbox((std::string(ICON_FA_BONE " ") + t("ui.vfx_table.bone_sel_show") + "##vfx_bone_selection").c_str(), &showBoneSelection);
    ImGui::EndTable();

    ImGui::PushItemWidth(-100);
    ImGui::Text(t("ui.vfx_table.loop_delay"));
    ImGui::DragFloat("##vfx_loop_delay", &loopDelayVFX, 0.1f, SETTINGS_FX_LOOP_DELAY_MIN, SETTINGS_FX_LOOP_DELAY_MAX, "%.1f");
    ImGui::PopItemWidth();

    ImGui::PushItemWidth(-100);
    ImGui::Text(t("ui.vfx_table.playback_dur"));
    ImGui::DragFloat("##vfx_duration_drag", &vfxDuration, 0.1f, SETTINGS_FX_DURATION_MIN, SETTINGS_FX_DURATION_MAX, "%.1f");
    ImGui::PopItemWidth();

    ImGui::Separator();
    ImGui::Text(t("ui.vfx_table.active"));

    {
        ChildScope childActive("##vfx_active_list", ImVec2(0, 220), true);
        if (childActive.open) {
            if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##vfx_active_refresh")) {
                Application::instance().engine().postGameThreadTask([this]() {
                    updateActiveVFX();
                });
            }
            ImGui::Separator();
            if (vfxEntries.empty()) {
                ImGui::TextDisabled(t("ui.vfx_table.no_active"));
            } else {
                for (size_t i = 0; i < vfxEntries.size(); ++i) {
                    VFXEntry& e = vfxEntries[i];
                    std::ostringstream ss;
                    ss << e.name << " on " << e.pawnName << " at " << e.boneName;
                    ImGui::Text("%s", ss.str().c_str());
                    ImGui::SameLine();
                    if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##" + e.name + std::to_string(i)).c_str())) {
                        Application::instance().engine().postGameThreadTask([this, i, pawnName = e.pawnName]() {
                            if (i < vfxEntries.size()) {
                                removeVFX(vfxEntries[i]);
                            }
                            Application::instance().animation().resetAnimation(pawnName);
                            updateActiveVFX();
                        });
                    }
                }
            }
        }
    }
    ImGui::Unindent();
}