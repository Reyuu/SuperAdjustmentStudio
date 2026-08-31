#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "application.h"
#include "gizmo.h"
#include "hook_manager.h"
#include "logger.h"
#include "sdk.h"
#include "util.h"
#include <sstream>

#include <LESDK/Common/Math.hpp>

#include "imgui.h"

// X=forward, Y=right, Z=up
static void RotatorToBasis(const FRotator& r, float outF[3], float outR[3], float outU[3]) {
    FMatrix m = MatrixCompose(FVector{0, 0, 0}, FVector{1, 1, 1}, UnrealRotationUnitsToRadians(r.Pitch), UnrealRotationUnitsToRadians(r.Yaw),
                              UnrealRotationUnitsToRadians(r.Roll));
    outF[0] = m.XPlane.X;
    outF[1] = m.XPlane.Y;
    outF[2] = m.XPlane.Z;
    outR[0] = m.YPlane.X;
    outR[1] = m.YPlane.Y;
    outR[2] = m.YPlane.Z;
    outU[0] = m.ZPlane.X;
    outU[1] = m.ZPlane.Y;
    outU[2] = m.ZPlane.Z;
}

// in-engine drawing
static void DrawWorldGizmo(ULineBatchComponent* lineBatcher, AActor* actor) {
    float ax[3];
    float ay[3];
    float az[3];

    RotatorToBasis(actor->Rotation, ax, ay, az);
    FVector origin{actor->Location.X, actor->Location.Y, actor->Location.Z};
    const float len = 80.0f;
    const float thickness = 2.5f;
    const float* axes[3] = {ax, ay, az};
    const FLinearColor colors[3] = {
        {1, 0, 0, 1}, // RED
        {0, 1, 0, 1}, // GREEN
        {0, 0, 1, 1}  // BLUE
    };
    // get options table
    void* self = GET_MEMBER_SLOT_POINTER(ULineBatchComponent, lineBatcher, FPrimitiveDrawInterfaceVfTable);
    auto DrawLine = lineBatcher->FPrimitiveDrawInterfaceVfTable->DrawLine;
    for (int i = 0; i < 3; ++i) {
        FVector end{origin.X + axes[i][0] * len, origin.Y + axes[i][1] * len, origin.Z + axes[i][2] * len};
        DrawLine(self, origin, end, colors[i], 1, thickness);
    }

    // stupid hack
    FVector endX{origin.X + ax[0] * len, origin.Y + ax[1] * len, origin.Z + ax[2] * len};
    DrawLine(self, origin, endX, colors[0], 1, 0);
}

static void DrawTracer(ULineBatchComponent* lineBatcher, const FVector& from, const FVector& to) {
    const FLinearColor yellow{1, 1, 0, 1};
    void* self = GET_MEMBER_SLOT_POINTER(ULineBatchComponent, lineBatcher, FPrimitiveDrawInterfaceVfTable);
    auto DrawLine = lineBatcher->FPrimitiveDrawInterfaceVfTable->DrawLine;
    DrawLine(self, from, to, yellow, 1, 3.0f);
    // another stupid hack
    DrawLine(self, from, to, yellow, 1, 0);
}

// always-on-top draw
static bool GetCameraView(ABioHUD* hud, FVector& loc, FVector& fwd) {
    APlayerController* pc = hud ? hud->PlayerOwner : nullptr;
    ACamera* cam = pc ? pc->PlayerCamera : nullptr;
    if (!cam) {
        return false;
    }

    FRotator rot;
    cam->GetCameraViewPoint(&loc, &rot);
    float f[3];
    float r[3];
    float u[3];
    RotatorToBasis(rot, f, r, u);
    fwd = {f[0], f[1], f[2]};
    return fwd.X != 0.0f || fwd.Y != 0.0f || fwd.Z != 0.0f;
}

static bool ClipSegment(const FVector& camLoc, const FVector& camFwd, float nearDist, FVector& a, FVector& b) {
    const float da = Dot(a - camLoc, camFwd) - nearDist;
    const float db = Dot(b - camLoc, camFwd) - nearDist;

    if (da < 0.0f && db < 0.0f) {
        return false;
    }
    if (da < 0.0f) {
        const float t = -da / (db - da);
        a.X += (b.X - a.X) * t;
        a.Y += (b.Y - a.Y) * t;
        a.Z += (b.Z - a.Z) * t;
    } else if (db < 0.0f) {
        const float t = -db / (da - db);
        b.X += (a.X - b.X) * t;
        b.Y += (a.Y - b.Y) * t;
        b.Z += (a.Z - b.Z) * t;
    }
    return true;
}

static void DrawWorldLineOnTop(ABioHUD* hud, FVector a, FVector b, const FColor& color) {
    UCanvas* canvas = hud ? hud->Canvas : nullptr;
    FVector camLoc;
    FVector camFwd;
    if (!canvas || !GetCameraView(hud, camLoc, camFwd)) {
        return;
    }
    if (!ClipSegment(camLoc, camFwd, 20.0f, a, b)) {
        return;
    }

    FVector sa = canvas->Project(a);
    FVector sb = canvas->Project(b);

    const float orgX = canvas->OrgX;
    const float orgY = canvas->OrgY;
    const float clipX = canvas->ClipX;
    const float clipY = canvas->ClipY;

    canvas->OrgX = 0.0f;
    canvas->OrgY = 0.0f;
    canvas->ClipX = (float)canvas->SizeX;
    canvas->ClipY = (float)canvas->SizeY;

    canvas->Draw2DLine(sa.X, sa.Y, sb.X, sb.Y, color);
    canvas->OrgX = orgX;
    canvas->OrgY = orgY;
    canvas->ClipX = clipX;
    canvas->ClipY = clipY;
}

static void DrawWorldGizmoOnTop(ABioHUD* hud, AActor* actor) {
    float ax[3];
    float ay[3];
    float az[3];

    RotatorToBasis(actor->Rotation, ax, ay, az);
    FVector origin = actor->Location;

    const float len = 80.0f;
    const float* axes[3] = {ax, ay, az};
    const FColor colors[3] = {
        {0,   0,   255, 255}, // RED
        {0,   255, 0,   255}, // GREEN
        {255, 0,   0,   255}  // BLUE
    };

    for (int i = 0; i < 3; i++) {
        FVector end{origin.X + axes[i][0] * len, origin.Y + axes[i][1] * len, origin.Z + axes[i][2] * len};
        DrawWorldLineOnTop(hud, origin, end, colors[i]);
    }
}

static void DrawTracerOnTop(ABioHUD* hud, const FVector& from, const FVector& to) {
    FColor yellow = {0, 255, 255, 255};
    DrawWorldLineOnTop(hud, from, to, yellow);
}

// wireframe bounding box
//    ________
//   /|      /
//  / |     /|
// /_______/ |
// |  |    | |
// |  |    | |
// |  |____|_|
// |  /    | |
// |_______|/

template <typename T> static void ForEachBoxEdge(const FVector& mn, const FVector& mx, T&& draw) {
    const FVector c[8] = {
        {mn.X, mn.Y, mn.Z},
        {mx.X, mn.Y, mn.Z},
        {mn.X, mx.Y, mn.Z},
        {mx.X, mx.Y, mn.Z},
        {mn.X, mn.Y, mx.Z},
        {mx.X, mn.Y, mx.Z},
        {mn.X, mx.Y, mx.Z},
        {mx.X, mx.Y, mx.Z},
    };
    static const int edges[12][2] = {
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
    for (int i = 0; i < 12; ++i) {
        draw(c[edges[i][0]], c[edges[i][1]]);
    }
}

static void DrawWorldBox(ULineBatchComponent* lineBatcher, const FBox& box) {
    void* self = GET_MEMBER_SLOT_POINTER(ULineBatchComponent, lineBatcher, FPrimitiveDrawInterfaceVfTable);
    auto DrawLine = lineBatcher->FPrimitiveDrawInterfaceVfTable->DrawLine;
    const FLinearColor color{1.0f, 0.5f, 0.0, 1.0f}; // orange
    ForEachBoxEdge(box.Min, box.Max, [&](const FVector& a, const FVector& b) {
        DrawLine(self, a, b, color, 1, 1.0f);
    });
    DrawLine(self, box.Min, FVector{box.Max.X, box.Min.Y, box.Min.Z}, color, 1, 0);
}

static void DrawWorldBoxOnTop(ABioHUD* hud, const FBox& box) {
    const FColor color{0, 128, 255, 255}; // orange
    ForEachBoxEdge(box.Min, box.Max, [&](const FVector& a, const FVector& b) {
        DrawWorldLineOnTop(hud, a, b, color);
    });
}

static bool GetActorBoundsBox(AActor* actor, FBox& out) {
    out = {};
    if (!actor) {
        return false;
    }

    actor->GetComponentsBoundingBox(&out);
    const bool isInvalidAABB = out.Max.X <= out.Min.X || out.Max.Y <= out.Min.Y || out.Max.Z <= out.Min.Z;
    if (!out.IsValid || isInvalidAABB) {
        const FVector& s = actor->DrawScale3D;
        const float hX = 50.0f * (s.X > 0.01f ? s.X : 1.0f);
        const float hY = 50.0f * (s.Y > 0.01f ? s.Y : 1.0f);
        const float hZ = 50.0f * (s.Z > 0.01f ? s.Z : 1.0f);

        out.Min = {actor->Location.X - hX, actor->Location.Y - hY, actor->Location.Z - hZ};
        out.Max = {actor->Location.X + hX, actor->Location.Y + hY, actor->Location.Z + hZ};
        out.IsValid = 1;
    }
    return out.IsValid != 0;
}

static APawn* GetPlayerPawn(ABioHUD* hud) {
    APlayerController* pc = hud ? hud->PlayerOwner : nullptr;
    return pc ? pc->Pawn : nullptr;
}

static bool isObjectStillLive(UObject* obj) {
    if (!UObject::GObjObjects || !obj) {
        return false;
    }
    for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
        if (UObject::GObjObjects->GetData()[i] == obj) {
            return true;
        }
    }
    return false;
}

void Gizmo::collectActorComponents(AActor* actor, std::vector<UActorComponent*>& out) {
    out.clear();
    if (!actor) {
        return;
    }

    auto append = [&](TArray<UActorComponent*>* arr) {
        if (!arr) {
            return;
        }
        for (int i = 0; i < (int)arr->Count(); ++i) {
            UActorComponent* c = arr->GetData()[i];
            if (c && c->Class && isObjectStillLive(c)) {
                out.push_back(c);
            }
        }
    };

    append(&actor->AllComponents);
    if (out.empty()) {
        append(&actor->Components);
    }
}

AActor* Gizmo::gizmoActor() {
    Application& app = Application::instance();

    if (isObjectStillLive(explicitTarget)) {
        return explicitTarget;
    }
    explicitTarget = nullptr;

    std::vector<std::string>& names = app.ui().pawnNames();
    int index = app.ui().pawnIndex();
    if (names.empty()) {
        gizmoActorPointer = nullptr;
        return nullptr;
    }
    const std::string& want = names[index];

    if (gizmoActorIndex != index || gizmoActorName != want) {
        gizmoActorPointer = nullptr;
    } else if (gizmoActorPointer) {
        if (!isObjectStillLive(gizmoActorPointer)) {
            gizmoActorPointer = nullptr;
        }
    }

    if (!gizmoActorPointer) {
        gizmoActorPointer = app.engine().findActorByName(want);
        gizmoActorIndex = index;
        gizmoActorName = want;
    }
    return gizmoActorPointer;
}

AActor* Gizmo::target() {
    return gizmoActor();
}

void Gizmo::setTarget(AActor* actor) {
    explicitTarget = actor;
    gizmoActorPointer = actor;
    if (actor) {
        gizmoActorIndex = Application::instance().ui().pawnIndex();
        gizmoActorName = FStringToUtf8(actor->GetName());
    } else {
        gizmoActorIndex = -1;
        gizmoActorName.clear();
    }
}

void Gizmo::clearExplicitTarget() {
    explicitTarget = nullptr;
    gizmoActorPointer = nullptr;
}

void Gizmo::pickFromScreen(ABioHUD* hud, float mouseX, float mouseY) {
    UCanvas* canvas = hud ? hud->Canvas : nullptr;
    if (!canvas) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float scaleX = (io.DisplaySize.x > 1.0f && io.DisplaySize.y > 1.0f) ? (float)canvas->SizeX / io.DisplaySize.x : 1.0f;
    const float scaleY = (io.DisplaySize.x > 1.0f && io.DisplaySize.y > 1.0f) ? (float)canvas->SizeY / io.DisplaySize.y : 1.0f;
    FVector2D screenPos{mouseX * scaleX, mouseY * scaleY};
    FVector origin, dir;
    canvas->DeProject(screenPos, &origin, &dir);
    if (dir.X == 0.0f && dir.Y == 0.0f && dir.Z == 0.0f) {
        return; // the ray is invalid
    }

    const float distance = 100000.0f;
    FVector end{origin.X + dir.X * distance, origin.Y + dir.Y * distance, origin.Z + dir.Z * distance};
    FVector hitLocation, hitNormal;
    FTraceHitInfo hitInfo;
    AActor* hit = nullptr;
    if (hud->PlayerOwner && isObjectStillLive(hud->PlayerOwner)) {
        hit = hud->PlayerOwner->Trace(end, origin, 1, FVector{0.0f, 0.0f, 0.0f}, 0, 0, &hitLocation, &hitNormal, &hitInfo);
    }
    if (hit && isObjectStillLive(hit)) {
        Application::instance().ui().selectActor(hit);
    }
}

void Gizmo::checkClickSelect(ABioHUD* hud) {
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; // mask test -> 0b1000000000000000 -> 0x8000
    if (down && !leftMouseButtonPreviouslyDown) {
        if (!ImGui::GetIO().WantCaptureMouse) {
            const ImVec2& mp = ImGui::GetIO().MousePos;
            pickFromScreen(hud, mp.x, mp.y);
        }
    }
    leftMouseButtonPreviouslyDown = down;
}

// event processing makes me want to commit sudoku

void Gizmo::processEvent(UObject* Context, UFunction* Function, void* Parms, void* Result) {
    ABioHUD* hud = nullptr;
    AActor* actor = nullptr;
    FBox box;
    bool drawOnTop = false;

    SAS_HOOK_TRY {
        if (Context && Context->IsA(ABioHUD::StaticClass()) && Function->GetName().Equals(L"PostRender")) {

            hud = static_cast<ABioHUD*>(Context);
            hud->FlushPersistentDebugLines();

            Application::instance().engine().applyHUDVisibility();

            const bool uiVisible = Application::instance().ui().showUI().load();
            if (uiVisible && clickSelectState) {
                checkClickSelect(hud);
            }
            actor = gizmoActor();
            if (actor && highlightSelectedState) {
                GetActorBoundsBox(actor, box);
            }
            if (debugAlwaysOnTopState) {
                drawOnTop = actor != nullptr;
            } else if (actor) {
                UWorld* world = GWorld ? *GWorld : nullptr;
                ULineBatchComponent* lb = world ? world->PersistentLineBatcher : nullptr;
                if (lb && lb->FPrimitiveDrawInterfaceVfTable) {
                    if (showGizmoState) {
                        DrawWorldGizmo(lb, actor);
                    }
                    if (highlightSelectedState && box.IsValid) {
                        DrawWorldBox(lb, box);
                    }

                    APawn* playerPawn = GetPlayerPawn(hud);
                    if (drawTracerState && playerPawn) {
                        DrawTracer(lb, playerPawn->Location, actor->Location);
                    }
                }
            }
        }
    } SAS_HOOK_CATCH_VOID

    if (origProcessEvent) {
        origProcessEvent(Context, Function, Parms, Result);
    }

    SAS_HOOK_TRY {
        if (hud) {
            Application::instance().bones().keepBonePoses();
        }

        if (drawOnTop && actor) {
            APawn* playerPawn = GetPlayerPawn(hud);
            if (showGizmoState) {
                DrawWorldGizmoOnTop(hud, actor);
            }
            if (highlightSelectedState && box.IsValid) {
                DrawWorldBoxOnTop(hud, box);
            }
            if (drawTracerState && playerPawn) {
                DrawTracerOnTop(hud, playerPawn->Location, actor->Location);
            }
        }
    } SAS_HOOK_CATCH_VOID
}

static void hkProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result) {
    SAS_HOOK_TRY {
        Application::instance().gizmo().processEvent(Context, Function, Parms, Result);
    } SAS_HOOK_CATCH_VOID
}

void Gizmo::initHooks(HookManager& hookManager, SDKContext& sdk) {
    if (sdk.processEventAddress() && sdk.initializer()) {
        origProcessEvent = (ProcessEventFn)hookManager.install("ProcessEvent", sdk.processEventAddress(), &hkProcessEvent);
        if (origProcessEvent) {
            Logger->debug("ProcessEvent hook installed");
        } else {
            Logger->debug("Failed to install ProcessEvent hook");
        }
    } else {
        Logger->debug("ProcessEvent address not resolved; world-space gizmo disabled.");
    }
}
