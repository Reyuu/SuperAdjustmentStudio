#ifndef SAS_BONES_H
#define SAS_BONES_H

#include <LESDK/Includes.LE2.hpp>
#include <atomic>
#include <mutex>
#include <string>
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
        std::atomic<bool>& bonePoseActive() {
            return bonePoseActiveState;
        }

        USkeletalMeshComponent* findPawnMeshForTarget(const std::string& pawnName, MeshTarget target);

        void listBones(const std::string& pawnName, MeshTarget target, std::vector<BonePoseInfo>& out);
        bool getBoneTransform(const std::string& pawnName, MeshTarget target, int index, BonePoseInfo& out);
        void setBonePose(const std::string& pawnName, MeshTarget target, const BonePoseInfo& pose);
        void resetBonePose(const std::string& pawnName, MeshTarget target);
        void absoluteResetBones(const std::string& pawnName, MeshTarget target);
        void keepBonePoses();

    private:
        struct BonePoseState {
            public:
                std::mutex mtx;
                std::vector<BonePoseInfo> posed;
                std::string pawn;
                MeshTarget target = MESH_BODY;
                std::vector<FBoneAtom> savedAtoms;
                std::vector<int> savedIndices;
                std::vector<int> pendingSnapshots;
                bool savedUseSavedPose = false;
                int boneCount = 0;
                bool toApply = false;
        };

        BonePoseState bonePose;
        std::atomic<bool> bonePoseActiveState{false};

        void restoreBonePoseMesh(std::string pawn, MeshTarget target, std::vector<FBoneAtom> atoms, std::vector<int> indices, bool useSavedPose);
        void releaseAllBonesFromPose(std::string pawn, MeshTarget target);
};

#endif // SAS_BONES_H