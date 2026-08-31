#ifndef SAS_VFX_H
#define SAS_VFX_H

#include <LESDK/Common/Math.hpp>
#include <LESDK/Includes.LE2.hpp>
#include <mutex>
#include <set>

struct VFXEntry {
    public:
        std::string name;
        std::string path;
        std::string pawnName;
        std::string boneName;
        float lifeTime = 9999.0f;
        double spawnTime = 0.0;
        ABioVisualEffect* actor = nullptr;
        UBioCameraShake* cameraShake = nullptr; // original template shake, for restore
        AActor* cameraShakenActor = nullptr;    // original shaken actor, for restore
        bool loop = false;
        double loopDelay = 0.0;    // seconds to wait after the effect ends before re-triggering
        double nextLoopTime = 0.0; // game time at which the next loop may fire (0 == not scheduled)
};

// orders/unique-ifies available templates by (case-insensitive) name
struct VFXTemplateNameLess {
        bool operator()(UBioVFXTemplate* a, UBioVFXTemplate* b) const;
};

class VFXManager {
    public:
        void renderUI();
        void findAvailableTemplates(bool forceRefresh = false);
        void addVFX(UBioVFXTemplate* vfxTemplate, AActor* actor, const std::string& boneName, float lifeTime, double spawnTime);
        void removeVFX(VFXEntry& entry);
        void removeAllVFX();
        void updateActiveVFX();

    private:
        void applyVFXLiveState(VFXEntry& entry);

    private:
        std::vector<VFXEntry> vfxEntries;
        std::set<UBioVFXTemplate*, VFXTemplateNameLess> availableTemplates;
        std::mutex vfxMtx;
        bool ignoreCameraMovement = false;
        bool loopVFX = false;
        float loopDelayVFX = 0.0f;
        float vfxDuration = 10.0f;
        bool showBoneSelection = false;
};

#endif // SAS_VFX_H