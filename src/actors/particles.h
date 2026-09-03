#ifndef SAS_PARTICLES_H
#define SAS_PARTICLES_H

#include "util.h"
#include <LESDK/Includes.LE2.hpp>
#include <LESDK/Common/Math.hpp>
#include <mutex>
#include <set>

struct ParticleEntry {
    public:
        std::string name;
        std::string path;
        std::string pawnName;
        float lifeTime = 9999.0f;
        double spawnTime = 0.0;
        UParticleSystemComponent* psc = nullptr;
        UParticleSystem* particleTemplate = nullptr; // for re-activation / loop
        AActor* emitterActor = nullptr;              // owner of the PSC, kept so it can be click-selected / moved via the gizmo
        bool loop = false;
        double loopDelay = 0.0;    // seconds to wait after the effect ends before re-triggering
        double nextLoopTime = 0.0; // game time at which the next loop may fire (0 == not scheduled)
};

// orders/unique-ifies available templates by (case-insensitive) name
struct ParticleTemplateNameLess {
        bool operator()(UParticleSystem* a, UParticleSystem* b) const;
};

class ParticleManager {
    public:
        void renderUI();
        void findAvailableTemplates(bool forceRefresh = false);
        void addParticle(UParticleSystem* particleTemplate, AActor* owner, const Transform& spawnTransform, float lifeTime, double spawnTime);
        void removeParticle(ParticleEntry& entry);
        void removeAllParticles();
        void updateActiveParticles();

    private:
        void applyParticleLiveState(ParticleEntry& entry);

    private:
        std::vector<ParticleEntry> particleEntries;
        std::set<UParticleSystem*, ParticleTemplateNameLess> availableTemplates;
        std::mutex particleMtx;
        bool loopParticles = false;
        float loopDelayParticles = 0.0f;
        float particleDuration = 10.0f;
};
#endif // SAS_PARTICLES_H
