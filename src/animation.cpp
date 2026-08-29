#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "animation.h"

#include <algorithm>
#include <sstream>

#include "application.h"
#include "logger.h"
#include "util.h"

static void collectTreeSequenceNodes(UAnimNode* node, std::vector<UAnimNodeSequence*>& out) {
    if (!node) {
        return;
    }
    if (node->IsA(UAnimNodeSequence::StaticClass())) {
        out.push_back((UAnimNodeSequence*)node);
        return;
    }
    if (node->IsA(UAnimNodeBlendBase::StaticClass())) {
        UAnimNodeBlendBase* blend = (UAnimNodeBlendBase*)node;
        for (int i = 0; i < (int)blend->Children.Count(); ++i) {
            collectTreeSequenceNodes(blend->Children.GetData()[i].Anim, out);
        }
    }
}

static void collectAllSequenceNodes(std::vector<UAnimNodeSequence*>& out) {
    forEachOf<USkeletalMeshComponent>([&](USkeletalMeshComponent* c) {
        if (c->Animations) {
            collectTreeSequenceNodes(c->Animations, out);
        }
    });
}

// freeze each sequence node play rate, since game's own pause does not stop skeletal animations... for some reason
void Animation::pauseAnimations(bool pause) {
    if (pause) {
        if (animPauseActiveState) {
            return;
        }

        pausedAnimNodes.clear();
        pausedAnimRates.clear();
        std::vector<UAnimNodeSequence*> nodes;
        collectAllSequenceNodes(nodes);
        for (UAnimNodeSequence* n : nodes) {
            pausedAnimNodes.push_back(n);
            pausedAnimRates.push_back(n->Rate);
            n->Rate = 0.0f;
        }
        animPauseActiveState = true;

        std::ostringstream ss;
        ss << "pauseAnimations: paused " << pausedAnimNodes.size() << " seq nodes";
        Logger->debug(ss.str());
    } else {
        if (!animPauseActiveState) {
            return;
        }
        for (int i = 0; i < (int)pausedAnimNodes.size(); ++i) {
            if (pausedAnimNodes[i]) {
                pausedAnimNodes[i]->Rate = pausedAnimRates[i];
            }
        }
        pausedAnimNodes.clear();
        pausedAnimRates.clear();
        animPauseActiveState = false;
        Logger->debug("pauseAnimations: resumed");
    }
}

void Animation::keepAnimationsPaused() {
    if (!animPauseActiveState) {
        return;
    }
    for (UAnimNodeSequence* n : pausedAnimNodes) {
        if (n) {
            n->Rate = 0.0f;
        }
    }
}

void Animation::resetAnimation(const std::string& pawnName) {
    USkeletalMeshComponent* mesh = Application::instance().engine().findPawnMesh(pawnName);
    if (!mesh) {
        Logger->debug("resetAnimation: skeletal mesh component not found on target");
        return;
    }

    if (customAnimSlot && isLiveObject(customAnimSlot)) {
        customAnimSlot->StopCustomAnim(0.2f);
    }
    customAnimSlot = nullptr;

    if (!mesh->Animations) {
        Logger->debug("resetAnimation: mesh has no anim tree root");
        return;
    }

    mesh->Animations->PlayAnim(true, 1.0f, 0.0f);

    UAnimTree* tree = nullptr;
    if (mesh->Animations->IsA(UAnimTree::StaticClass())) {
        tree = (UAnimTree*)mesh->Animations;
    }
    if (!tree) {
        forEachOf<UAnimTree>([&](UAnimTree* t) {
            if (!tree && t->Outer == mesh) {
                tree = t;
            }
        });
    }
    if (tree) {
        tree->bUseSavedPose = false;
    }

    playedNode = nullptr;
    playedNodeOrigSeq = nullptr;
    playedNodeOrigName = SFXName();

    std::ostringstream ss;
    ss << "resetAnimation: released custom anim and resumed locomotion on '" << pawnName << "'";
    Logger->debug(ss.str());
}

// TODO: more robust way of searching for unlinked animations and linking them from other pckgs
static bool linkAnimSetForSequence(USkeletalMeshComponent* mesh, const std::string& animName) {
    if (!mesh || animName.empty()) {
        return false;
    }

    UAnimSet* found = nullptr;
    forEachOf<UAnimSet>([&](UAnimSet* set) {
        if (found) {
            return;
        }
        for (int j = 0; j < (int)set->Sequences.Count(); ++j) {
            UAnimSequence* seq = set->Sequences.GetData()[j];
            if (!seq) {
                continue;
            }
            const char* nm = seq->SequenceName.GetName();
            if (nm && animName == nm) {
                found = set;
                return;
            }
        }
    });
    if (!found) {
        return false;
    }

    for (int i = 0; i < (int)mesh->AnimSets.Count(); ++i) {
        if (mesh->AnimSets.GetData()[i] == found) {
            return true;
        }
    }
    mesh->AnimSets.Add(found);

    std::ostringstream ss;
    ss << "playAnimation: linked set '" << FStringToUtf8(found->GetName()) << "' to resolve '" << animName << "'";
    Logger->debug(ss.str());
    return true;
}

void Animation::playAnimation(const std::string& pawnName, const std::string& animName, bool bLoop) {
    if (animName.empty()) {
        Logger->debug("playAnimation: empty anim name");
        return;
    }

    AActor* actor = Application::instance().engine().findActorByName(pawnName);
    if (!actor) {
        Logger->debug("playAnimation: target pawn not found");
        return;
    }
    if (!actor->IsA(ABioPawn::StaticClass())) {
        Logger->debug("playAnimation: target is not a BioPawn (no body-stance anim slots)");
        return;
    }

    ABioPawn* pawn = (ABioPawn*)actor;
    USkeletalMeshComponent* mesh = Application::instance().engine().findPawnMesh(pawnName);
    if (!mesh) {
        Logger->debug("playAnimation: skeletal mesh component not found on target");
        return;
    }

    linkAnimSetForSequence(mesh, animName);

    std::vector<UAnimNodeSequence*> nodes;
    collectTreeSequenceNodes(mesh->Animations, nodes);
    SFXName sfxAnim(animName.c_str(), 0);

    TArray<UAnimNodeSlot*>& slots = pawn->BodyStanceNodes;
    if (slots.Count() == 0) {
        pawn->CacheAnimNodes();
    }
    if (slots.Count() == 0) {
        std::ostringstream ss;
        ss << "playAnimation: '" << animName << "' on '" << pawnName << "' failed (no body-stance slots in tree; " << nodes.size() << " seq nodes)";
        Logger->debug(ss.str());
        return;
    }

    UAnimNodeSlot* slot = slots.GetData()[0];
    for (int i = 0; i < (int)slots.Count(); ++i) {
        UAnimNodeSlot* s = slots.GetData()[i];
        if (!s) {
            continue;
        }
        const char* nm = s->NodeName.GetName();
        std::string snm = nm ? nm : "";
        if (snm.find("FullBody") != std::string::npos || snm.find("Body") != std::string::npos) {
            slot = s;
            break;
        }
    }

    mesh->StopAnim();
    float pos = pawn->PawnPlayCustomAnim(slot, sfxAnim, 1.0f, 0.1f, 0.2f, bLoop ? 1 : 0, 1, 0.0f);
    customAnimSlot = slot;

    std::ostringstream ss;
    ss << "playAnimation: '" << animName << "' on '" << pawnName << "' via PawnPlayCustomAnim slot='"
       << (slot->NodeName.GetName() ? slot->NodeName.GetName() : "?") << "' (slots=" << slots.Count() << ", seqNodes=" << nodes.size() << ", pos=" << pos
       << ")";
    Logger->debug(ss.str());
}