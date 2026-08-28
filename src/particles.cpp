#include "particles.h"
#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"
#include "application.h"
#include "imgui.h"
#include "logger.h"
#include "util.h"
#include <LESDK/Common/Core.hpp>
#include <LESDK/Includes.LE2.hpp>
#include <sstream>

static UParticleSystem* findParticleTemplateByName(const std::string& name) {
    UParticleSystem* found = nullptr;
    forEachOf<UParticleSystem>([&](UParticleSystem* p) {
        if (p && toLowerStr(FStringToUtf8(p->GetName())).find(toLowerStr(name)) != std::string::npos) {
            found = p;
        }
    });
    return found;
}

bool ParticleTemplateNameLess::operator()(UParticleSystem* a, UParticleSystem* b) const {
    return toLowerStr(FStringToUtf8(a->GetName())) < toLowerStr(FStringToUtf8(b->GetName()));
}

void ParticleManager::findAvailableTemplates(bool forceRefresh) {
    std::lock_guard<std::mutex> lock(particleMtx);
    if (!forceRefresh && !availableTemplates.empty()) {
        return;
    }
    availableTemplates.clear();
    forEachOf<UParticleSystem>([&](UParticleSystem* p) {
        if (p) {
            availableTemplates.insert(p);
        }
    });
}

static AEmitter* spawnEmitterActor(UParticleSystem* emitterTemplate, AActor* owner, const Transform& spawnTransform) {
    if (!GWorld || !*GWorld) {
        Logger->debug("spawnEmitterActor: GWorld is null");
        return nullptr;
    }
    if (!emitterTemplate) {
        Logger->debug("spawnEmitterActor: missing template");
        return nullptr;
    }
    if (!isLiveObject(owner)) {
        Logger->debug("spawnEmitterActor: spawn owner is not live");
        return nullptr;
    }

    FVector loc;
    loc.X = spawnTransform.pos[0];
    loc.Y = spawnTransform.pos[1];
    loc.Z = spawnTransform.pos[2];
    FRotator rot;
    rot.Pitch = DegreesToUnrealRotationUnits(spawnTransform.rot[0]);
    rot.Yaw = DegreesToUnrealRotationUnits(spawnTransform.rot[1]);
    rot.Roll = DegreesToUnrealRotationUnits(spawnTransform.rot[2]);

    AActor* spawned = owner->Spawn(AEmitter::StaticClass(), nullptr, SFXName(), loc, rot, nullptr, nullptr, 1, 0);
    AEmitter* emitter = static_cast<AEmitter*>(spawned);
    if (!emitter || !emitter->ParticleSystemComponent) {
        Logger->debug("spawnEmitterActor: failed to spawn AEmitter");
        if (emitter) {
            emitter->Destroy();
        }
        return nullptr;
    }
    emitter->bDestroyOnSystemFinish = 0;
    emitter->ParticleSystemComponent->SetTemplate(emitterTemplate);
    emitter->ParticleSystemComponent->bWasCompleted = 0;
    emitter->ParticleSystemComponent->SetActive(1);
    return emitter;
}

void ParticleManager::applyParticleLiveState(ParticleEntry& entry) {
    if (!isLiveObject(entry.psc) || !isLiveObject(entry.particleTemplate)) {
        return;
    }
    UParticleSystemComponent* psc = entry.psc;
    psc->SetTemplate(entry.particleTemplate);
    psc->bWasCompleted = 0;
    psc->SetActive(1);
}

void ParticleManager::addParticle(UParticleSystem* particleTemplate, AActor* owner, const Transform& spawnTransform, float lifeTime, double spawnTime) {
    if (!isLiveObject(particleTemplate)) {
        Logger->debug("addParticle: particleTemplate is not live");
        return;
    }
    if (!isLiveObject(owner)) {
        Logger->debug("addParticle: owner is not live");
        return;
    }

    AEmitter* emitter = spawnEmitterActor(particleTemplate, owner, spawnTransform);
    if (!emitter) {
        Logger->debug("addParticle: failed to spawn emitter");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(particleMtx);
        ParticleEntry entry;
        entry.name = FStringToUtf8(particleTemplate->GetName());
        entry.path = FStringToUtf8(particleTemplate->GetFullName());
        entry.pawnName = FStringToUtf8(owner->GetName());
        entry.lifeTime = lifeTime;
        entry.spawnTime = spawnTime;
        entry.psc = emitter->ParticleSystemComponent;
        entry.particleTemplate = particleTemplate;
        entry.emitterActor = emitter;
        entry.loop = loopParticles;
        particleEntries.push_back(entry);
    }
    applyParticleLiveState(particleEntries.back());
}

void ParticleManager::removeParticle(ParticleEntry& entry) {
    std::lock_guard<std::mutex> lock(particleMtx);
    if (isLiveObject(entry.psc)) {
        entry.psc->SetActive(0);
    }
    entry.psc = nullptr;
    if (isLiveObject(entry.emitterActor)) {
        entry.emitterActor->Destroy();
    }
    entry.emitterActor = nullptr;
    for (auto it = particleEntries.begin(); it != particleEntries.end(); ++it) {
        if (&(*it) == &entry) {
            particleEntries.erase(it);
            break;
        }
    }
}

void ParticleManager::removeAllParticles() {
    std::lock_guard<std::mutex> lock(particleMtx);
    for (ParticleEntry& entry : particleEntries) {
        if (isLiveObject(entry.psc)) {
            entry.psc->SetActive(0);
        }
        entry.psc = nullptr;
        if (isLiveObject(entry.emitterActor)) {
            entry.emitterActor->Destroy();
        }
        entry.emitterActor = nullptr;
    }
    particleEntries.clear();
}

void ParticleManager::updateActiveParticles() {
    std::lock_guard<std::mutex> lock(particleMtx);
    double now = ImGui::GetTime();
    for (auto it = particleEntries.begin(); it != particleEntries.end();) {
        ParticleEntry& e = *it;

        bool pscLive = isLiveObject(e.psc);
        bool actorLive = isLiveObject(e.emitterActor);
        if (!pscLive || !actorLive) {
            if (pscLive) {
                e.psc->SetActive(0);
            }
            if (actorLive) {
                e.emitterActor->Destroy();
            }
            e.psc = nullptr;
            e.emitterActor = nullptr;
            it = particleEntries.erase(it);
            continue;
        }

        if (e.loop) {
            if (!e.psc->bIsActive || e.psc->bWasCompleted) {
                applyParticleLiveState(e);
            }
            ++it;
        } else if ((now - e.spawnTime) >= (double)e.lifeTime) {
            e.psc->SetActive(0);
            e.psc = nullptr;
            if (e.emitterActor) {
                e.emitterActor->Destroy();
                e.emitterActor = nullptr;
            }
            it = particleEntries.erase(it);
        } else {
            ++it;
        }
    }
}

void ParticleManager::renderUI() {
    if (!ImGui::CollapsingHeader("Particles")) {
        return;
    }

    static std::string selectedParticleName;
    findAvailableTemplates();

    {
        static float lastAutoUpdate = 0.0f;
        if (ImGui::GetTime() - lastAutoUpdate > 0.25f) {
            lastAutoUpdate = ImGui::GetTime();
            Application::instance().engine().postGameThreadTask([this]() {
                updateActiveParticles();
            });
        }
    }

    ImGui::Indent();
    if (ImGui::Button("Load BioD_ProNor package")) {
        LoadPackageByName("BioD_ProNor");
    }

    ImGui::Separator();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##particle_available_refresh")) {
        findAvailableTemplates(true);
    }
    ImGui::SameLine();
    ImGui::Text("Available Particles");
    static char particleSearchFilter[256] = "";
    ImGui::InputTextWithHint("##particle_search", "Search particles...", particleSearchFilter, sizeof(particleSearchFilter));
    std::string filterLower = toLowerStr(particleSearchFilter);
    if (ImGui::BeginCombo("##particle_available", selectedParticleName.c_str())) {
        for (UParticleSystem* p : availableTemplates) {
            if (!p) {
                continue;
            }
            std::string name = FStringToUtf8(p->GetName());
            if (!filterLower.empty() && toLowerStr(name).find(filterLower) == std::string::npos) {
                continue;
            }
            if (ImGui::Selectable(name.c_str())) {
                selectedParticleName = name;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Loop##particle_loop", &loopParticles)) {
        Application::instance().engine().postGameThreadTask([this]() {
            std::lock_guard<std::mutex> lock(particleMtx);
            for (ParticleEntry& entry : particleEntries) {
                entry.loop = loopParticles;
            }
        });
    }

    ImGui::InputFloat("Duration##particle_duration", &particleDuration);

    if (ImGui::Button("Spawn##particle_spawn_btn")) {
        std::string pawnName = Application::instance().ui().getSelectedPawnName();
        Transform spawnTransform = Application::instance().ui().getSpawnTransform();
        if (pawnName.empty()) {
            Logger->debug("particle spawn: no pawn selected");
        } else if (selectedParticleName.empty()) {
            Logger->debug("particle spawn: no template selected");
        } else {
            double spawnTime = ImGui::GetTime();
            Application::instance().engine().postGameThreadTask(
                [this, pawnName, particleName = selectedParticleName, spawnTransform, lifeTime = particleDuration, spawnTime]() {
                    AActor* resolvedActor = Application::instance().engine().findActorByName(pawnName);
                    if (!resolvedActor) {
                        Logger->debug("particle spawn: selected pawn no longer exists");
                        return;
                    }
                    UParticleSystem* resolvedTemplate = findParticleTemplateByName(particleName);
                    if (!resolvedTemplate) {
                        Logger->debug("particle spawn: template no longer exists");
                        return;
                    }
                    addParticle(resolvedTemplate, resolvedActor, spawnTransform, lifeTime, spawnTime);
                });
        }
    }

    ImGui::Separator();

    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##particle_active_refresh")) {
        Application::instance().engine().postGameThreadTask([this]() {
            updateActiveParticles();
        });
    }
    ImGui::SameLine();
    ImGui::Text("Active Particles");
    if (particleEntries.empty()) {
        ImGui::TextDisabled("(no active particles)");
    } else {
        for (size_t i = 0; i < particleEntries.size(); ++i) {
            ParticleEntry& entry = particleEntries[i];
            std::ostringstream ss;
            ss << entry.name << " on " << entry.pawnName;
            ImGui::Text("%s", ss.str().c_str());
            ImGui::SameLine();
            if (ImGui::Button((std::string("Select##") + entry.name + std::to_string(i)).c_str())) {
                if (isLiveObject(entry.emitterActor)) {
                    Application::instance().ui().selectActor(entry.emitterActor);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(ICON_FA_MINUS) + "##" + entry.name + std::to_string(i)).c_str())) {
                Application::instance().engine().postGameThreadTask([this, i]() {
                    if (i < particleEntries.size()) {
                        removeParticle(particleEntries[i]);
                    }
                });
            }
        }
    }
    ImGui::Unindent();
}
