#include "bones.h"
#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <cmath>
#include <cstring>
#include <numbers>
#include <sstream>

#include "application.h"
#include "logger.h"
#include "util.h"

#include <LESDK/Common/Math.hpp>
#include <LESDK/Includes.hpp>

#include "tracy.h"

static const int kParentSpace = 2; // EBoneSpace::BS_Parent

#pragma region // Helpers
static UAnimTree* findAnimTree(USkeletalMeshComponent* mesh) {
    if (!mesh) {
        return nullptr;
    }
    if (mesh->Animations && mesh->Animations->IsA(UAnimTree::StaticClass())) {
        return (UAnimTree*)mesh->Animations;
    }
    UAnimTree* found = nullptr;
    forEachOf<UAnimTree>([&](UAnimTree* t) {
        if (!found && t->Outer == mesh) {
            found = t;
        }
    });
    return found;
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

void Bones::restoreBonePoseMesh(std::string pawn, MeshTarget target, std::vector<FBoneAtom> atoms, std::vector<int> indicies, bool useSavedPose) {

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
    ss << "ResetBonePose: '" << pawn << "' mesh=" << (target == MESH_HEAD ? "head" : "body") << " restored=" << atoms.size() << " atoms";
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
    ss << "absoluteReset: '" << pawn << "' mesh=" << (target == MESH_HEAD ? "head" : "body") << " released";
    Logger->debug(ss.str());
}

void Bones::keepBonePoses() {
    ZoneScopedN("Bones::keepBonePoses");
    struct EntrySnapshot {
            BonePoseState state;
    };
    std::vector<EntrySnapshot> entries;
    {
        std::lock_guard<std::mutex> lock(poseMtx);
        if (poses.empty()) {
            return;
        }
        for (auto& kv : poses) {
            entries.push_back({kv.second});
        }
    }

    for (auto& snap : entries) {
        BonePoseState& st = snap.state;
        const std::string& pawn = st.pawn;
        MeshTarget target = st.target;

        USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawn, target);
        if (!mesh || !mesh->SkeletalMesh) {
            continue;
        }

        int n = (int)mesh->SkeletalMesh->RefSkeleton.Count();
        if (st.boneCount > 0 && n != st.boneCount) {
            Logger->debug("keepBonePoses: skeleton swapped; dropping pose");
            resetBonePose(pawn, target);
            continue;
        }

        if ((int)mesh->LocalAtoms.Count() != n) {
            Logger->debug("keepBonePoses: LocalAtoms not bone-indexed (count mismatch); pose disabled");
            resetBonePose(pawn, target);
            continue;
        }

        UAnimTree* tree = findAnimTree(mesh);
        if (!tree) {
            Logger->debug("keepBonePoses: no anim tree on target mesh; pose disabled");
            Bones::resetBonePose(pawn, target);
            continue;
        }

        if (!st.pendingSnapshots.empty()) {
            std::lock_guard<std::mutex> lock(poseMtx);
            auto it = poses.find({pawn, (int)target});
            if (it == poses.end()) {
                continue;
            }
            BonePoseState& live = it->second;
            for (size_t k = 0; k < live.pendingSnapshots.size(); ++k) {
                int index = live.pendingSnapshots[k];
                if (index < 0 || index >= (int)mesh->LocalAtoms.Count()) {
                    continue;
                }
                FBoneAtom orig = mesh->LocalAtoms.GetData()[index];
                live.savedAtoms.push_back(orig);
                live.savedIndices.push_back(index);
                // use the base position captured at the time the bone was added to pendingSnapshots
                FVector anchor = orig.Translation;
                if (k < live.pendingBasePos.size()) {
                    anchor = live.pendingBasePos[k];
                }
                live.savedBasePos.push_back(anchor);
                st.savedAtoms.push_back(orig);
                st.savedIndices.push_back(index);
                st.savedBasePos.push_back(anchor);
            }
            live.savedUseSavedPose = tree->bUseSavedPose;
            live.boneCount = n;
            live.pendingSnapshots.clear();
            live.pendingBasePos.clear();
            st.savedUseSavedPose = live.savedUseSavedPose;
            st.boneCount = n;
            st.pendingSnapshots.clear();
            st.pendingBasePos.clear();
        }

        if ((int)tree->SavedPose.Count() != n) {
            tree->SavedPose.Resize(n);
        }

        FBoneAtom* sp = tree->SavedPose.GetData();
        FBoneAtom* la = mesh->LocalAtoms.GetData();
        memcpy(sp, la, n * sizeof(FBoneAtom));

        for (const BonePoseInfo& p : st.posed) {
            if (p.index < 0 || p.index >= n) {
                continue;
            }
            FBoneAtom& a = sp[p.index];
            a.Rotation = mesh->QuatFromRotator(
                mesh->MakeRotator(DegreesToUnrealRotationUnits(p.rot[0]), DegreesToUnrealRotationUnits(p.rot[1]), DegreesToUnrealRotationUnits(p.rot[2])));

            FVector base = la[p.index].Translation;
            for (size_t k = 0; k < st.savedIndices.size() && k < st.savedBasePos.size(); ++k) {
                if (st.savedIndices[k] == p.index) {
                    base = st.savedBasePos[k];
                    break;
                }
            }
            a.Translation.X = base.X + p.pos[0];
            a.Translation.Y = base.Y + p.pos[1];
            a.Translation.Z = base.Z + p.pos[2];
            a.Scale = (p.scale[0] > 0.0f) ? p.scale[0] : 1.0f;
        }
        tree->bUseSavedPose = true;
        if (st.toApply) {
            bool cleared = false;
            {
                std::lock_guard<std::mutex> lock(poseMtx);
                auto it = poses.find({pawn, (int)target});
                if (it != poses.end() && it->second.toApply) {
                    it->second.toApply = false;
                    cleared = true;
                }
            }
            if (cleared) {
                mesh->ForceSkelUpdate();
            }
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

    USkeletalMesh* skel = mesh->SkeletalMesh;
    TArray<int>& ref = skel->RefSkeleton;
    int n = (int)ref.Count();

    // read NameIndexMap directly
    constexpr ptrdiff_t kNameMapOffset = 0xC8; // USkeletalMesh::NameIndexMap, no SDK accessor
    std::vector<SFXName> ordered(static_cast<size_t>(n));
    std::vector<bool> have(static_cast<size_t>(n), false);
    int mapFound = 0;
    auto& nameMap = *reinterpret_cast<TMap<SFXName, int>*>(reinterpret_cast<char*>(skel) + kNameMapOffset);
    for (const auto& kv : nameMap) {
        if (kv.Value < 0 || kv.Value >= n) {
            continue;
        }
        ordered[static_cast<size_t>(kv.Value)] = kv.Key;
        have[static_cast<size_t>(kv.Value)] = true;
        ++mapFound;
    }
    if (mapFound == 0) {
        TArray<SFXName> fbNames;
        mesh->GetBoneNames(&fbNames);
        int nc = (int)fbNames.Count();
        for (int i = 0; i < nc && i < n; ++i) {
            ordered[static_cast<size_t>(i)] = fbNames.GetData()[i];
            have[static_cast<size_t>(i)] = true;
        }
    }

    for (int i = 0; i < n; ++i) {
        BonePoseInfo b;
        b.index = i;
        b.parentIndex = ref.GetData()[i];
        if (have[static_cast<size_t>(i)]) {
            const char* nm = ordered[static_cast<size_t>(i)].GetName();
            b.boneName = nm ? nm : "";
        } else {
            b.boneName = "bone_" + std::to_string(i);
        }
        if (b.parentIndex >= 0 && b.parentIndex < n && have[static_cast<size_t>(b.parentIndex)]) {
            const char* pnm = ordered[static_cast<size_t>(b.parentIndex)].GetName();
            b.parentName = pnm ? pnm : "";
        }
        out.push_back(b);
    }
}

bool Bones::getBoneTransform(const std::string& pawnName, MeshTarget target, int index, BonePoseInfo& out) {
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
        Logger->debug(std::string("getBoneTransform: index out of range ") + "(" + std::to_string(index) + "out of " + std::to_string((int)ref.Count()) + ")");
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
    out.pos[0] = loc.X;
    out.pos[1] = loc.Y;
    out.pos[2] = loc.Z;
    if (mesh->LocalAtoms.Count() == (int)ref.Count()) {
        const FBoneAtom& atom = mesh->LocalAtoms.GetData()[index];
        out.scale[0] = out.scale[1] = out.scale[2] = atom.Scale;
        FRotator rot = mesh->QuatToRotator(atom.Rotation);
        out.rot[0] = UnrealRotationUnitsToDegrees(rot.Pitch);
        out.rot[1] = UnrealRotationUnitsToDegrees(rot.Yaw);
        out.rot[2] = UnrealRotationUnitsToDegrees(rot.Roll);
    } else {
        out.scale[0] = out.scale[1] = out.scale[2] = 1.0f;
        out.rot[0] = out.rot[1] = out.rot[2] = 0.0f;
    }
    return true;
}

bool Bones::getBoneWorldBasis(const std::string& pawnName, MeshTarget target, int index, FVector& outPos, FVector& outX, FVector& outY, FVector& outZ) {
    USkeletalMeshComponent* mesh = findPawnMeshForTarget(pawnName, target);
    if (!mesh || !mesh->SkeletalMesh) {
        static std::string lastFailKey;
        std::string key = pawnName + "/" + std::to_string((int)target) + "/" + std::to_string(index) + ":nomesh";
        if (key != lastFailKey) {
            lastFailKey = key;
            Logger->debug("getBoneWorldBasis: no mesh for '" + key + "'");
        }
        return false;
    }
    int n = (int)mesh->SkeletalMesh->RefSkeleton.Count();
    if (index < 0 || index >= n) {
        return false;
    }
    // engine-computed bone-frame-to-world transform (no space/enum assumptions on our side)
    SFXName sname = mesh->GetBoneName(index);
    FVector zero{0.0f, 0.0f, 0.0f};
    FRotator zeroRot{0, 0, 0};
    FRotator worldRot{0, 0, 0};
    mesh->TransformFromBoneSpace(sname, zero, zeroRot, &outPos, &worldRot);
    FMatrix basis = MatrixCompose(FVector{0.0f, 0.0f, 0.0f}, FVector{1.0f, 1.0f, 1.0f}, UnrealRotationUnitsToRadians(worldRot.Pitch),
                                  UnrealRotationUnitsToRadians(worldRot.Yaw), UnrealRotationUnitsToRadians(worldRot.Roll));
    outX = basis.XPlane;
    outY = basis.YPlane;
    outZ = basis.ZPlane;
    auto norm = [](FVector& v) {
        float l = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        if (l > 1e-8f) {
            v.X /= l;
            v.Y /= l;
            v.Z /= l;
        }
    };
    norm(outX);
    norm(outY);
    norm(outZ);
    return true;
}

void Bones::setBonePose(const std::string& pawnName, MeshTarget target, const BonePoseInfo& pose) {
    FVector capturedBase{0, 0, 0};
    bool hasCapturedBase = false;
    {
        std::lock_guard<std::mutex> lock(poseMtx);
        BonePoseState& entry = poses[{pawnName, (int)target}];
        entry.pawn = pawnName;
        entry.target = target;

        int index = -1;
        for (int i = 0; i < (int)entry.posed.size(); ++i) {
            if (entry.posed[i].index == pose.index) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            BonePoseInfo b;
            b.index = pose.index;
            entry.posed.push_back(b);
            entry.pendingSnapshots.push_back(pose.index);
            index = (int)entry.posed.size() - 1;
            // capture base position now, before LocalAtoms is modified by later edits
            USkeletalMeshComponent* m = findPawnMeshForTarget(pawnName, target);
            if (m && m->SkeletalMesh && pose.index >= 0 && pose.index < (int)m->LocalAtoms.Count()) {
                capturedBase = m->LocalAtoms.GetData()[pose.index].Translation;
                hasCapturedBase = true;
            }
            if (hasCapturedBase) {
                entry.pendingBasePos.push_back(capturedBase);
            }
        }
        entry.posed[index] = pose;
        entry.toApply = true;
    }
}

void Bones::resetBonePose(const std::string& pawnName, MeshTarget target) {
    std::vector<FBoneAtom> atoms;
    std::vector<int> indices;
    bool useSavedPose = false;
    bool hadData = false;
    {
        std::lock_guard<std::mutex> lock(poseMtx);
        auto it = poses.find({pawnName, (int)target});
        if (it != poses.end()) {
            hadData = !it->second.savedAtoms.empty();
            if (hadData) {
                atoms = it->second.savedAtoms;
                indices = it->second.savedIndices;
                useSavedPose = it->second.savedUseSavedPose;
            }
            poses.erase(it);
        }
    }
    if (hadData) {
        Application::instance().engine().postGameThreadTask([this, pawnName, target, atoms, indices, useSavedPose]() {
            restoreBonePoseMesh(pawnName, target, atoms, indices, useSavedPose);
        });
    } else {
        std::ostringstream ss;
        ss << "resetBonePose: '" << pawnName << "' mesh=" << (target == MESH_HEAD ? "head" : "body") << " (nothing to restore)";
        Logger->debug(ss.str());
    }
}

void Bones::resetAllBonePoses() {
    std::map<std::pair<std::string, int>, BonePoseState> doomed;
    {
        std::lock_guard<std::mutex> lock(poseMtx);
        doomed = poses;
        poses.clear();
    }
    if (doomed.empty()) {
        return;
    }
    Application::instance().engine().postGameThreadTask([this, doomed = std::move(doomed)]() {
        for (auto& kv : doomed) {
            if (!kv.second.savedAtoms.empty()) {
                restoreBonePoseMesh(kv.second.pawn, kv.second.target, kv.second.savedAtoms, kv.second.savedIndices, kv.second.savedUseSavedPose);
            }
        }
    });
}

void Bones::absoluteResetBones(const std::string& pawnName, MeshTarget target) {
    {
        std::lock_guard<std::mutex> lock(poseMtx);
        poses.erase({pawnName, (int)target});
    }
    Application::instance().engine().postGameThreadTask([this, pawnName, target]() {
        releaseAllBonesFromPose(pawnName, target);
    });
}
#pragma endregion
