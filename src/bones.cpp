#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"
#include "bones.h"

#include <cmath>
#include <numbers>
#include <cstring>
#include <sstream>

#include "logger.h"
#include "util.h"
#include "application.h"

#include <LESDK/Common/Math.hpp>

static const int kParentSpace = 2; // EBoneSpace::BS_Parent

#pragma region // Helpers
static UAnimTree* findAnimTree(USkeletalMeshComponent* mesh) {
    if (!mesh) {
        return nullptr;
    }
    if (mesh->Animations && mesh->Animations->IsA(UAnimTree::StaticClass())) {
        return (UAnimTree*)mesh->Animations;
    }
    if (UObject::GObjObjects) {
        for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
            UObject* o = UObject::GObjObjects->GetData()[i];
            if (!o) {
                continue;
            }
            if (!o->IsA(UAnimTree::StaticClass())) {
                return (UAnimTree*)mesh->Animations;
            }
            if (UObject::GObjObjects) {
                for (int i = 0; i < (int)UObject::GObjObjects->Count(); ++i) {
                    UObject* o = UObject::GObjObjects->GetData()[i];
                    if (!o) {
                        continue;
                    }
                    if (!o->IsA(UAnimTree::StaticClass())) {
                        continue;
                    }

                    UAnimTree* t = (UAnimTree*)o;
                    if (t->Outer == mesh) {
                        return t;
                    }
                }
            }
        }
    }
    return nullptr;
}

#pragma endregion

USkeletalMeshComponent* Bones::findPawnMeshForTarget(const std::string& pawnName, MeshTarget target) {
    AActor* actor = Application::instance().engine().findActorByName(pawnName);
    if (!actor) {
        return nullptr;
    }
    if (target == MESH_HEAD) {
        if (!actor->IsA(ABioPawn::StaticClass())) {
            return nullptr;
        }
        return ((ABioPawn*)actor)->HeadMesh;
    }

    if (!actor->IsA(APawn::StaticClass())) {
        return nullptr;
    }
    return ((APawn*)actor)->Mesh;
}


#pragma region // Direct bone posing

void Bones::restoreBonePoseMesh(std::string pawn, MeshTarget target, std::vector<FBoneAtom> atoms, \
                                std::vector<int> indicies, bool useSavedPose) {
    
    USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawn, target);
    if (mesh && mesh->SkeletalMesh) {
        FBoneAtom* la = mesh->LocalAtoms.GetData();
        for (int i = 0; i < indicies.size(); ++i) {
            if (indicies[i] >= 0 && indicies[i] < (int)mesh->LocalAtoms.Count()) {
                la[indicies[i]] = atoms[i];
            }
        }
        UAnimTree* tree = findAnimTree(mesh);
        if (tree) {
            tree->bUseSavedPose = useSavedPose;
        }
        mesh->ForceSkelUpdate();
    }

    std::ostringstream ss;
    ss  << "ResetBonePose: '" << pawn << "' mesh=" << (target == MESH_HEAD ? "head" : "body")
        << " restored=" << atoms.size() << " atoms";
    Logger->debug(ss.str());
}

void Bones::releaseAllBonesFromPose(std::string pawn, MeshTarget target) {
    USkeletalMeshComponent* mesh = Bones::findPawnMeshForTarget(pawn, target);
    if (mesh && mesh->SkeletalMesh) {
        UAnimTree* tree = findAnimTree(mesh);
        if (tree) {
            tree->bUseSavedPose = false;
        }
        mesh->ForceSkelUpdate();
    }

    std::ostringstream ss;
    ss  << "absoluteReset: '" << pawn << "' mesh=" << (target == MESH_HEAD ? "head" : "body")
        << " released";
    Logger->debug(ss.str());
}

void Bones::keepBonePoses() {
    std::vector<BonePoseInfo> posed;
    std::vector<int> pending;
    std::vector<FBoneAtom> baseAtoms;
    std::vector<int> baseIndices;
    std::string pawn;
    MeshTarget target;
    int boneCount = 0;
    bool toApply = false;

    {
        std::lock_guard<std::mutex> lock(bonePose.mtx);
        if (!bonePoseActiveState.load()) {
            return;
        }
        posed = bonePose.posed;
        pending = bonePose.pendingSnapshots;
        baseAtoms = bonePose.savedAtoms;
        baseIndices = bonePose.savedIndices;
        pawn = bonePose.pawn;
        target = bonePose.target;
        boneCount = bonePose.boneCount;
        toApply = bonePose.toApply;
    }

    USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawn, target);
    if (!mesh || !mesh->SkeletalMesh) {
        return;
    }
    
    int n = (int)mesh->SkeletalMesh->RefSkeleton.Count();
    if (boneCount > 0 && n != boneCount) {
        Logger->debug("keepBonePoses: skeleton swapped; dropping pose");
        resetBonePose(pawn, target);
        return;
    }

    if ((int)mesh->LocalAtoms.Count() != n) {
        Logger->debug("keepBonePoses: LocalAtoms not bone-indexed (count mismatch); pose disabled");
        resetBonePose(pawn, target);
        return;
    }

    UAnimTree* tree = findAnimTree(mesh);
    if (!tree) {
        Logger->debug("keepBonePoses: no anim tree on target mesh; pose disabled");
        Bones::resetBonePose(pawn, target);
    }

    if (!pending.empty()) {
        std::lock_guard<std::mutex> lock(bonePose.mtx);
        for (int index : pending) {
            if (index < 0 || index >= (int)mesh->LocalAtoms.Count()) {
                continue;
            }
            FBoneAtom orig = mesh->LocalAtoms.GetData()[index];
            bonePose.savedAtoms.push_back(orig);
            bonePose.savedIndices.push_back(index);
            baseAtoms.push_back(orig);
            baseIndices.push_back(index);
        }
        bonePose.savedUseSavedPose = tree->bUseSavedPose;
        bonePose.boneCount = n;
        bonePose.pendingSnapshots.clear();
    }

    if ((int)tree->SavedPose.Count() != n) {
        tree->SavedPose.Resize(n);
    }

    FBoneAtom* sp = tree->SavedPose.GetData();
    FBoneAtom* la = mesh->LocalAtoms.GetData();
    memcpy(sp, la, n * sizeof(FBoneAtom));

    for (const BonePoseInfo& p : posed) {
        if (p.index < 0 || p.index >= n) {
            continue;
        }
        FBoneAtom& a = sp[p.index];
        a.Rotation = mesh->QuatFromRotator(mesh->MakeRotator(
            DegreesToUnrealRotationUnits(p.rot[0]),
            DegreesToUnrealRotationUnits(p.rot[1]),
            DegreesToUnrealRotationUnits(p.rot[2])
        ));

        FVector base = la[p.index].Translation;
        for(int k = 0; k < baseIndices.size(); ++k) {
            if (baseIndices[k] == p.index) {
                base = baseAtoms[k].Translation;
                break;
            }
        }
        a.Translation.X = base.X + p.pos[0];
        a.Translation.Y = base.Y + p.pos[1];
        a.Translation.Z = base.Z + p.pos[2];
        a.Scale = (p.scale[0] > 0.0f) ? p.scale[0] : 1.0f;
    }
    tree->bUseSavedPose = true;
    if (toApply) {
        bool cleared = false;
        {
            std::lock_guard<std::mutex> lock(bonePose.mtx);
            if (bonePose.toApply) {
                bonePose.toApply = false;
                cleared = true;
            }
        }
        if (cleared) {
            mesh->ForceSkelUpdate();
        }
    }
}

void Bones::listBones(const std::string& pawnName, MeshTarget target, std::vector<BonePoseInfo>& out) {
    out.clear();
    USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawnName, target);
    if (!mesh) {
        Logger->debug("listBones: target mesh not found");
        return;
    }
    if (!mesh->SkeletalMesh) {
        Logger->debug("listBones: mesh has no SkeletalMesh");
        return;
    }
    
    TArray<int>& ref = mesh->SkeletalMesh->RefSkeleton;
    int n = (int)ref.Count();
    TArray<SFXName> names;
    mesh->GetBoneNames(&names);
    if ((int)names.Count() < n) {
        n = (int)names.Count();
    }

    std::ostringstream ss;
    ss  << "listBones: '" << pawnName << "' mesh=" << (target == MESH_HEAD ? "head" : "body")
        << " bones=" << n << " ref=" << ref.Count()
        << " spaceBases=" << mesh->SpaceBases.Count()
        << " localAtoms=" << mesh->LocalAtoms.Count();
    Logger->debug(ss.str());

    for (int i = 0; i < n; ++i) {
        BonePoseInfo b;
        b.index = i;
        b.parentIndex = ref.GetData()[i];
        SFXName bn = names.GetData()[i];
        b.boneName = bn.GetName() ? bn.GetName() : "";
        if (b.parentIndex >= 0 && b.parentIndex < n) {
            SFXName pn = names.GetData()[b.parentIndex];
            b.parentName = pn.GetName() ? pn.GetName() : "";
        }
        out.push_back(b);
    }
}

bool Bones::getBoneTransform(const std::string& pawnName, MeshTarget target, int index, BonePoseInfo& out)
{
    USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawnName, target);
    if (!mesh) {
        Logger->debug("getBoneTransform: target mesh not found");
        return false;
    }
    if (!mesh->SkeletalMesh) {
        Logger->debug("getBoneTransform: mesh has no SkeletalMesh");
        return false;
    }

    TArray<int>& ref = mesh->SkeletalMesh->RefSkeleton;
    if (index < 0 || index >= (int)ref.Count()) {
        Logger->debug(std::string("getBoneTransform: index out of range ")
                    + "(" + std::to_string(index) + "out of " + std::to_string((int)ref.Count()) + ")");
        return false;
    }

    out.index = index;
    out.parentIndex = ref.GetData()[index];
    SFXName bn = mesh->GetBoneName(index);
    out.boneName = bn.GetName() ? bn.GetName() : "";
    out.parentName.clear();
    if (out.parentIndex >= 0) {
        SFXName pn = mesh->GetBoneName(out.parentIndex);
        out.parentName = pn.GetName() ? pn.GetName() : "";
    }

    SFXName sname(out.boneName.c_str(), 0);
    FVector loc = mesh->GetBoneLocation(sname, kParentSpace);
    FQuat quat = mesh->GetBoneQuaternion(sname, kParentSpace);
    out.pos[0] = loc.X;
    out.pos[1] = loc.Y;
    out.pos[2] = loc.Z;
    out.scale[0] = out.scale[1] = out.scale[2] = mesh->LocalAtoms.GetData()[index].Scale;
    if (mesh->LocalAtoms.Count() == (int)ref.Count()) {
        out.scale[0] = out.scale[1] = out.scale[2] = mesh->LocalAtoms.GetData()[index].Scale;
    }

    FRotator rot = mesh->QuatToRotator(quat);
    out.rot[0] = UnrealRotationUnitsToDegrees(rot.Pitch);
    out.rot[1] = UnrealRotationUnitsToDegrees(rot.Yaw);
    out.rot[2] = UnrealRotationUnitsToDegrees(rot.Roll);
    return true;
}

void Bones::setBonePose(const std::string& pawnName, MeshTarget target, const BonePoseInfo& pose)
{
    std::string oldPawn;
    MeshTarget oldTarget = MESH_BODY;
    std::vector<FBoneAtom> oldAtoms;
    std::vector<int> oldIndices;
    bool oldUseSavedPose = false;
    bool hadOldData = false;
    {
        std::lock_guard<std::mutex> lock(bonePose.mtx);
        if (bonePoseActiveState.load() && (bonePose.pawn != pawnName || bonePose.target != target)) {
            oldPawn = bonePose.pawn;
            oldTarget = bonePose.target;
            oldAtoms = bonePose.savedAtoms;
            oldIndices = bonePose.savedIndices;
            oldUseSavedPose = bonePose.savedUseSavedPose;
            hadOldData = !oldAtoms.empty();
            bonePose.posed.clear();
            bonePose.savedAtoms.clear();
            bonePose.savedIndices.clear();
            bonePose.pendingSnapshots.clear();
            bonePose.toApply = false;
            bonePose.boneCount = 0;
            bonePoseActiveState.store(false);
        }
        bonePose.pawn = pawnName;
        bonePose.target = target;
        bonePoseActiveState.store(true);

        int index = -1;
        for (int i = 0; i < (int)bonePose.posed.size(); ++i) {
            if (bonePose.posed[i].index == pose.index) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            BonePoseInfo b;
            b.index = pose.index;
            bonePose.posed.push_back(b);
            bonePose.pendingSnapshots.push_back(pose.index);
            index = (int)bonePose.posed.size() - 1;
        }
        bonePose.posed[index] = pose;
        bonePose.toApply = true;
    }

    if (hadOldData) {
        Application::instance().engine().postGameThreadTask([this, oldPawn, oldTarget, oldAtoms, oldIndices, oldUseSavedPose]() {
            restoreBonePoseMesh(oldPawn, oldTarget, oldAtoms, oldIndices, oldUseSavedPose);
        });
    }
}

void Bones::resetBonePose(const std::string& pawnName, MeshTarget target)
{
    std::vector<FBoneAtom> atoms;
    std::vector<int> indices;
    bool useSavedPose = false;
    bool hadData = false;
    {
        std::lock_guard<std::mutex> lock(bonePose.mtx);
        hadData = bonePoseActiveState.load() && !bonePose.savedAtoms.empty();
        if (hadData) {
            atoms = bonePose.savedAtoms;
            indices = bonePose.savedIndices;
            useSavedPose = bonePose.savedUseSavedPose;
        }
        bonePose.posed.clear();
        bonePose.savedAtoms.clear();
        bonePose.savedIndices.clear();
        bonePose.pendingSnapshots.clear();
        bonePose.toApply = false;
        bonePose.boneCount = 0;
        bonePoseActiveState.store(false);
    }
    if (hadData) {
        Application::instance().engine().postGameThreadTask([this, pawnName, target, atoms, indices, useSavedPose]() {
            restoreBonePoseMesh(pawnName, target, atoms, indices, useSavedPose);
        });
    } else {
        std::ostringstream ss;
        ss  << "resetBonePose: '" << pawnName << "' mesh=" << (target == MESH_HEAD ? "head" : "body")
            << " (nothing to restore)";
        Logger->debug(ss.str());
    }
}

void Bones::absoluteResetBones(const std::string& pawnName, MeshTarget target)
{
    {
        std::lock_guard<std::mutex> lock(bonePose.mtx);
        bonePose.posed.clear();
        bonePose.savedAtoms.clear();
        bonePose.savedIndices.clear();
        bonePose.pendingSnapshots.clear();
        bonePose.toApply = false;
        bonePose.boneCount = 0;
        bonePoseActiveState.store(false);
    }
    Application::instance().engine().postGameThreadTask([this, pawnName, target]() {
        releaseAllBonesFromPose(pawnName, target);
    });
}
#pragma endregion

