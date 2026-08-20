#ifndef SAS_GIZMO_H
#define SAS_GIZMO_H

#include <string>
#include <vector>
#include "util.h"
#include "hook_manager.h"
#include "sdk.h"

#include <LESDK/Includes.LE2.hpp>

#define GET_MEMBER_SLOT_POINTER(cls, parent, request) (BYTE*)parent + offsetof(cls, request);

typedef void(*ProcessEventFn)(UObject* Context, UFunction* Function, void* Parms, void* Result);

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

        void initHooks(HookManager& hooks, SDKContext& sdk);
        void setTarget(AActor* actor);
        void collectActorComponents(AActor* actor, std::vector<UActorComponent*>& out);
        void processEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

    private:
        bool showGizmoState = false;
        bool drawTracerState = false;
        bool debugAlwaysOnTopState = false;
        bool highlightSelectedState = false;
        bool clickSelectState = false;

        // original UObject::ProcessEvent, to forward after batching
        ProcessEventFn origProcessEvent = nullptr;

        // cached target actor
        AActor* gizmoActorPointer = nullptr;
        int gizmoActorIndex = -1;
        std::string gizmoActorName;

        bool leftMouseButtonPreviouslyDown = false;

        AActor* gizmoActor();
        void pickFromScreen(ABioHUD* hud, float mouseX, float mouseY);
        void checkClickSelect(ABioHUD* hud);
};

#endif //SAS_GIZMO_H