#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"
#include "IconsFontAwesome6.h"
#include "application.h"
#include "imgui.h"
#include "logger.h"
#include "ui.h"
#include "ui_helpers/raii_guards.h"
#include "ui_helpers/toast_notifications.h"
#include "util.h"

#include "vfx.h"
#include <LESDK/Includes.LE2.hpp>

static UBioVFXTemplate* findVFXTemplateByName(const std::string& name) {
    UBioVFXTemplate* vfxTemplate = nullptr;
    forEachOf<UBioVFXTemplate>([&](UBioVFXTemplate* vfx) {
        if (vfx && toLowerStr(FStringToUtf8(vfx->GetName())).find(toLowerStr(name)) != std::string::npos) {
            vfxTemplate = vfx;
        }
    });

    return vfxTemplate;
}

void VFXManager::addVFX(UBioVFXTemplate* vfxTemplate, AActor* actor, const std::string& boneName, float lifeTime, double spawnTime) {
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

    // if (vfxTemplate->bIsCrustEffect) {...}

    ABioVisualEffect* vfxActor = nullptr;
    vfxActor = donor->CreateCrustEffect(vfxTemplate, actor, lifeTime, 0);
    // attach to bone
    if (!vfxActor && actor->IsA(APawn::StaticClass())) {
        vfxActor = donor->CreateVFXOnMesh(vfxTemplate, actor, SFXName(boneName.c_str(), 0), lifeTime, static_cast<APawn*>(actor)->Mesh, 0);
    }

    if (!vfxActor && actor->IsA(ASFXPawn::StaticClass())) {
        ASFXPawn* sfxPawn = static_cast<ASFXPawn*>(actor);
        sfxPawn->CreateVisualEffect(vfxTemplate, &vfxActor);
    }

    // go on, make it live
    if (vfxActor) {
        // capture originals before any camera stripping, for later restore
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

void VFXManager::removeVFX(VFXEntry& entry) {
    std::lock_guard<std::mutex> lock(vfxMtx);
    if (entry.actor) {
        entry.actor->SetPaused(1, true);
        entry.actor->SetLifeTime(0.0f);
        entry.actor->LoopDuration(0);
        entry.actor->PauseOnDeath(1);
        entry.actor->fStateDurations[0] = 0.0f; // SPAWN
        entry.actor->fStateDurations[1] = 0.0f; // LIFE
        entry.actor->bActive = 0;
        entry.actor->bPaused = 1;
        entry.actor->SetState(2, true, true, false); // BVFX_DE
        entry.actor->eventOnComplete();
        entry.actor = nullptr;
    }

    // pop the entry from the list
    for (auto it = vfxEntries.begin(); it != vfxEntries.end(); ++it) {
        if (&(*it) == &entry) {
            vfxEntries.erase(it);
            break;
        }
    }
}

void VFXManager::removeAllVFX() {
    for (VFXEntry& entry : vfxEntries) {
        if (entry.actor) {
            removeVFX(entry);
        }
    }
}

void VFXManager::applyVFXLiveState(VFXEntry& entry) {
    ABioVisualEffect* a = entry.actor;
    if (!a) {
        return;
    }
    if (ignoreCameraMovement) {
        // strip any camera movement the VFX template would apply
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
    a->bDeleteSelf = 0; // keep the revived effect alive; otherwise the engine would delete it right after we re-trigger
    // eCurrentState is an enum, 0=SPAWN, 1=LIFE, 2=DEATH
    a->SetState(1, true, true, false); // BVFX_LIFE
}

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
            // re-trigger a finished effect so it visibly loops, waiting loopDelay between cycles
            bool ended = !e.actor->bActive || e.actor->bDeleteSelf || e.actor->eCurrentState == 2;
            if (ended) {
                if (e.nextLoopTime == 0.0) {
                    // effect just ended: schedule the next re-trigger after the wait duration
                    e.nextLoopTime = now + e.loopDelay;
                } else if (now >= e.nextLoopTime) {
                    applyVFXLiveState(e);
                    e.nextLoopTime = 0.0;
                }
            }
            ++it;
        } else if ((now - e.spawnTime) >= (double)e.lifeTime) {
            // auto-remove once our own lifetime has elapsed
            it = vfxEntries.erase(it);
        } else {
            ++it;
        }
    }
}

bool VFXTemplateNameLess::operator()(UBioVFXTemplate* a, UBioVFXTemplate* b) const {
    return toLowerStr(FStringToUtf8(a->GetName())) < toLowerStr(FStringToUtf8(b->GetName()));
}

void VFXManager::findAvailableTemplates(bool forceRefresh) {
    std::lock_guard<std::mutex> lock(vfxMtx);
    if (!forceRefresh && !availableTemplates.empty()) {
        return;
    }
    availableTemplates.clear();
    forEachOf<UBioVFXTemplate>([&](UBioVFXTemplate* vfx) {
        if (vfx && vfx->bIsInitialized) {
            availableTemplates.insert(vfx);
        }
    });
}

void VFXManager::renderUI() {
    if (!ImGui::CollapsingHeader(ICON_FA_FIRE " VFX")) {
        return;
    }

    static std::string selectedVFXName;
    findAvailableTemplates(); // populate availableTemplates on first use

    // periodically auto-remove expired (non-looping) VFX from the active list
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
    // list available templates
    // allow user to select a template and spawn it on a selected pawn
    static std::string boneSelect;
    std::vector<BonePoseInfo> bones;
    Application::instance().bones().listBones(Application::instance().ui().getSelectedPawnName(), MESH_BODY, bones);
    if (showBoneSelection) {
        if (boneSelect.empty() && !bones.empty()) {
            boneSelect = bones[0].boneName;
        }
        ImGui::Text("Bone:");
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
        // select first bone by default if no selection
        boneSelect = bones.empty() ? "" : bones[0].boneName;
    }

    ImGui::Separator();
    ImGui::Text("Available VFX:");
    ImGui::Text(ICON_FA_MAGNIFYING_GLASS);
    ImGui::SameLine();
    ImGui::PushItemWidth(-100);
    static char vfxSearchFilter[256] = "";
    ImGui::InputText("##class_search", vfxSearchFilter, sizeof(vfxSearchFilter));
    std::string filterLower = toLowerStr(vfxSearchFilter);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT)) {
        findAvailableTemplates(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##vfx_spawn_btn")) {
        // spawn the selected VFX on the selected pawn
        AActor* actor = Application::instance().engine().findActorByName(Application::instance().ui().getSelectedPawnName());
        if (actor) {
            UBioVFXTemplate* vfxTemplate = findVFXTemplateByName(selectedVFXName);
            if (vfxTemplate) {
                double spawnTime = ImGui::GetTime();
                Application::instance().engine().postGameThreadTask([this, vfxTemplate, actor, boneSelect = boneSelect, spawnTime]() {
                    addVFX(vfxTemplate, actor, boneSelect.c_str(), vfxDuration, spawnTime);
                });
            }
        }
    }
    {
        ChildScope child("##vfx_available_list", ImVec2(0, 120), true);
        if (child.open) {
            for (UBioVFXTemplate* vfxTemplate : availableTemplates) {
                if (!vfxTemplate) {
                    continue;
                }
                std::string name = FStringToUtf8(vfxTemplate->GetName());
                std::string lowerName = toLowerStr(name);
                if (!filterLower.empty() && lowerName.find(filterLower) == std::string::npos) {
                    continue;
                }
                if (ImGui::Selectable(name.c_str(), selectedVFXName == name)) {
                    selectedVFXName = name;
                }
            }
        }
    }

    ImGui::Separator();

    ImGui::BeginTable("##vfx_table", 2);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::Checkbox(ICON_FA_CAMERA " Ignore camera movement##vfx_ignore_cam", &ignoreCameraMovement)) {
        // apply/restore the camera-shake setting on all currently active VFX
        Application::instance().engine().postGameThreadTask([this]() {
            std::lock_guard<std::mutex> lock(vfxMtx);
            for (VFXEntry& entry : vfxEntries) {
                if (!entry.actor) {
                    continue;
                }
                if (ignoreCameraMovement) {
                    entry.actor->m_cameraShake = nullptr;
                    entry.actor->m_cameraShakenActor = nullptr;
                } else {
                    entry.actor->m_cameraShake = entry.cameraShake;
                    entry.actor->m_cameraShakenActor = entry.cameraShakenActor;
                }
            }
        });
    }
    ImGui::TableNextColumn();

    if (ImGui::Checkbox(ICON_FA_REPEAT " Loop##vfx_loop", &loopVFX)) {
        // apply/restore looping on all currently active VFX
        Application::instance().engine().postGameThreadTask([this]() {
            std::lock_guard<std::mutex> lock(vfxMtx);
            for (VFXEntry& entry : vfxEntries) {
                if (entry.actor) {
                    entry.actor->LoopDuration(loopVFX ? 1 : 0);
                    entry.loop = loopVFX;
                    entry.loopDelay = loopDelayVFX;
                    entry.nextLoopTime = 0.0;
                }
            }
        });
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Checkbox(ICON_FA_BONE " Show bone selection##vfx_bone_selection", &showBoneSelection);
    ImGui::EndTable();

    ImGui::PushItemWidth(-100);
    ImGui::Text("Loop delay:");
    ImGui::DragFloat("##vfx_loop_delay", &loopDelayVFX, 0.1f, 0.0f, 60.0f, "%.1f");
    ImGui::PopItemWidth();

    ImGui::PushItemWidth(-100);
    ImGui::Text("Playback duration:");
    ImGui::DragFloat("##vfx_duration_drag", &vfxDuration, 0.1f, 0.1f, 60.0f, "%.1f");
    ImGui::PopItemWidth();

    ImGui::Separator();

    ImGui::Text("Active:");
    {
        ChildScope childActive("##vfx_active_list", ImVec2(0, 220), true);
        if (childActive.open) {
            // list active VFX as selectables
            // allow user to remove a VFX
            // align the refresh button to the left
            if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##vfx_active_refresh")) {
                // refresh the active list: prune dead entries / re-loop
                Application::instance().engine().postGameThreadTask([this]() {
                    updateActiveVFX();
                });
            }
            ImGui::Separator();
            if (vfxEntries.empty()) {
                ImGui::TextDisabled("(no active VFX)");
            } else {
                for (size_t i = 0; i < vfxEntries.size(); ++i) {
                    VFXEntry& entry = vfxEntries[i];
                    std::ostringstream ss;
                    ss << entry.name << " on " << entry.pawnName << " at " << entry.boneName;
                    ImGui::Text("%s", ss.str().c_str());
                    ImGui::SameLine();
                    if (ImGui::Button((std::string(ICON_FA_MINUS) + "##" + entry.name + std::to_string(i)).c_str())) {
                        // capture the index and pawn name, not references, since the vector can be resized before this task runs
                        Application::instance().engine().postGameThreadTask([this, i, pawnName = entry.pawnName]() {
                            if (i < vfxEntries.size()) {
                                removeVFX(vfxEntries[i]);
                            }
                            // reset the pawn's animation and refresh the active list
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