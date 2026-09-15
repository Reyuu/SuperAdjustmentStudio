#ifndef SAS_LE3_COMPAT_H
#define SAS_LE3_COMPAT_H

#include <LESDK/Includes.hpp>

#ifdef SDK_TARGET_LE3

// FGuid has no operator== in the SDK, so compare fields directly.
inline bool IsGuidZero(const FGuid& g) {
    return g.A == 0 && g.B == 0 && g.C == 0 && g.D == 0;
}

// The live ASFXGRI owns the client-effect manager (m_pClientEffectManager).
inline URvrClientEffectManager* GetCEManager() {
    URvrClientEffectManager* mgr = nullptr;
    forEachOf<ASFXGRI>([&](ASFXGRI* gri) {
        if (mgr || !gri) {
            return;
        }
        mgr = gri->m_pClientEffectManager;
    });
    return mgr;
}

#endif // SDK_TARGET_LE3

#endif // SAS_LE3_COMPAT_H
