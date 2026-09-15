#ifndef SAS_GIZMO_H
#define SAS_GIZMO_H

#include "hook_manager.h"
#include "sdk.h"
#include "settings.h"
#include "util.h"
#include <string>
#include <vector>

#include <LESDK/Includes.hpp>

#define GET_MEMBER_SLOT_POINTER(cls, parent, request) (BYTE*)parent + offsetof(cls, request);

typedef void (*ProcessEventFn)(UObject* Context, UFunction* Function, void* Parms, void* Result);

static const int cubeEdges[12][2] = {
    {0, 1},
    {1, 3},
    {3, 2},
    {2, 0},
    {4, 5},
    {5, 7},
    {7, 6},
    {6, 4},
    {0, 4},
    {1, 5},
    {2, 6},
    {3, 7},
};

class Gizmo {
    public:
        bool& showGizmo() {
            return showGizmoState;
        }
        bool& drawTracer() {
            return drawTracerState;
        }
        bool& debugAlwaysOnTop() {
            return debugAlwaysOnTopState;
        }
        bool& highlightSelected() {
            return highlightSelectedState;
        }
        bool& clickSelect() {
            return clickSelectState;
        }
        bool& showBonePivot() {
            return showBonePivotState;
        }

        void initHooks(HookManager& hooks, SDKContext& sdk);
        void setTarget(AActor* actor);
        void clearExplicitTarget();
        AActor* target();
        void collectActorComponents(AActor* actor, std::vector<UActorComponent*>& out);
        void processEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

    private:
        bool showGizmoState = SETTINGS_TOGGLE_OFF;
        bool drawTracerState = SETTINGS_TOGGLE_OFF;
        bool debugAlwaysOnTopState = SETTINGS_TOGGLE_OFF;
        bool highlightSelectedState = SETTINGS_TOGGLE_OFF;
        bool clickSelectState = SETTINGS_TOGGLE_OFF;
        bool showBonePivotState = SETTINGS_TOGGLE_OFF;

        // original UObject::ProcessEvent, to forward after batching
        ProcessEventFn origProcessEvent = nullptr;

        // cached target actor
        AActor* gizmoActorPointer = nullptr;
        int gizmoActorIndex = -1;
        std::string gizmoActorName;

        AActor* explicitTarget = nullptr;

        bool leftMouseButtonPreviouslyDown = false;

        AActor* gizmoActor();
        AActor* selectNextOnLostSelection();
        void pickFromScreen(ABioHUD* hud, float mouseX, float mouseY);
        void checkClickSelect(ABioHUD* hud);
};

#endif // SAS_GIZMO_H