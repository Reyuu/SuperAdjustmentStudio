#ifndef SAS_ANIMATION_H
#define SAS_ANIMATION_H

#include <LESDK/Includes.LE2.hpp>
#include <string>
#include <vector>

class Animation {
    public:
        bool& animPauseActive() {
            return animPauseActiveState;
        }

        void pauseAnimations(bool pause);
        void keepAnimationsPaused();
        void resetAnimation(const std::string& pawnName);
        void playAnimation(const std::string& pawnName, const std::string& animName, bool bLoop = true);

    private:
        bool animPauseActiveState = false;
        UAnimNodeSequence* playedNode = nullptr;
        SFXName playedNodeOrigName;
        UAnimSequence* playedNodeOrigSeq = nullptr;
        UAnimNodeSlot* customAnimSlot = nullptr;

        std::vector<UAnimNodeSequence*> pausedAnimNodes;
        std::vector<float> pausedAnimRates;
};

#endif // SAS_ANIMATION_H