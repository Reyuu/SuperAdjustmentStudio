#ifndef SAS_BONES_H
#define SAS_BONES_H

#include <LESDK/Includes.hpp>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

enum MeshTarget : int {
    MESH_BODY = 0,
    MESH_HEAD = 1
};

struct BonePoseInfo {
    public:
        std::string boneName;
        int index = -1;
        int parentIndex = -1;
        std::string parentName;
        float pos[3] = {0, 0, 0};
        float rot[3] = {0, 0, 0};
        float scale[3] = {1, 1, 1};
};

class Bones {
    public:
        // true if any pawn has an active pose
        bool bonePoseActive() {
            std::lock_guard<std::mutex> lock(poseMtx);
            return !poses.empty();
        }
        bool bonePoseActive(const std::string& pawnName, MeshTarget target) {
            std::lock_guard<std::mutex> lock(poseMtx);
            return poses.find({pawnName, (int)target}) != poses.end();
        }

        USkeletalMeshComponent* findPawnMeshForTarget(const std::string& pawnName, MeshTarget target);

        void listBones(const std::string& pawnName, MeshTarget target, std::vector<BonePoseInfo>& out);
        bool getBoneTransform(const std::string& pawnName, MeshTarget target, int index, BonePoseInfo& out);
        bool getBoneWorldBasis(const std::string& pawnName, MeshTarget target, int index, FVector& outPos, FVector& outX, FVector& outY, FVector& outZ);
        void setBonePose(const std::string& pawnName, MeshTarget target, const BonePoseInfo& pose);
        void resetBonePose(const std::string& pawnName, MeshTarget target);
        void resetAllBonePoses();
        void absoluteResetBones(const std::string& pawnName, MeshTarget target);
        void keepBonePoses();

        struct BonePoseState {
            public:
                std::vector<BonePoseInfo> posed;
                std::string pawn;
                MeshTarget target = MESH_BODY;
                std::vector<FBoneAtom> savedAtoms;
                std::vector<int> savedIndices;
                std::vector<FVector> savedBasePos;
                std::vector<int> pendingSnapshots;
                std::vector<FVector> pendingBasePos;
                bool savedUseSavedPose = false;
                int boneCount = 0;
                bool toApply = false;
        };

        // multi-actor poses keyed by (pawn, target); guarded by poseMtx
        std::mutex poseMtx;
        std::map<std::pair<std::string, int>, BonePoseState> poses;

    private:
        void restoreBonePoseMesh(std::string pawn, MeshTarget target, std::vector<FBoneAtom> atoms, std::vector<int> indices, bool useSavedPose);
        void releaseAllBonesFromPose(std::string pawn, MeshTarget target);
};

#endif // SAS_BONES_H