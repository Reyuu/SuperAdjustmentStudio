#ifndef SAS_VFX_H
#define SAS_VFX_H

#include <LESDK/Includes.hpp>
#include <LESDK/Common/Math.hpp>
#include <mutex>
#include <set>
#include <string>
#include "settings.h"

#ifdef SDK_TARGET_LE3
#include "le3_compat.h"
#endif

#ifdef SDK_TARGET_LE3
using VfxTemplateT = URvrClientEffectInterface;
#else
using VfxTemplateT = UBioVFXTemplate;
#endif

struct VFXEntry {
        std::string name;
        std::string path;
        std::string pawnName;
        std::string boneName;
        float lifeTime = 9999.0f;
        double spawnTime = 0.0;

#ifdef SDK_TARGET_LE3
        URvrClientEffectInterface* templateRef = nullptr;
        FGuid effectGuid{0, 0, 0, 0}; // zero-init so IsGuidZero works
        AActor* effectOwner = nullptr;
#else
        ABioVisualEffect* actor = nullptr;
        UBioCameraShake* cameraShake = nullptr; // original shake, restored when toggled back
        AActor* cameraShakenActor = nullptr;
#endif

        bool loop = false;
        double loopDelay = 0.0;    // wait after the effect ends before re-triggering
        double nextLoopTime = 0.0; // 0 means not scheduled
};

// orders available templates by case-insensitive name
struct VFXTemplateNameLess {
        bool operator()(VfxTemplateT* a, VfxTemplateT* b) const;
};

class VFXManager {
    public:
        void renderUI();
        void findAvailableTemplates(bool forceRefresh = false);
        void addVFX(VfxTemplateT* vfxTemplate, AActor* actor, const std::string& boneName, float lifeTime, double spawnTime);
        void removeVFX(VFXEntry& entry);
        void removeAllVFX();
        void updateActiveVFX();
        static VfxTemplateT* findTemplateByName(const std::string& name);

        std::vector<VFXEntry> vfxEntries;
        std::mutex vfxMtx;

    private:
        void applyVFXLiveState(VFXEntry& entry);

        std::set<VfxTemplateT*, VFXTemplateNameLess> availableTemplates;
        bool ignoreCameraMovement = SETTINGS_TOGGLE_OFF;
        bool loopVFX = SETTINGS_TOGGLE_OFF;
        float loopDelayVFX = SETTINGS_FX_LOOP_DELAY_DEFAULT;
        float vfxDuration = SETTINGS_FX_DURATION_DEFAULT;
        bool showBoneSelection = SETTINGS_TOGGLE_OFF;
};

#endif // SAS_VFX_H
