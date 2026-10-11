#ifndef SAS_ANIMATION_H
#define SAS_ANIMATION_H

#include <LESDK/Includes.hpp>
#include <map>
#include <string>
#include <vector>

struct PlayedAnimState {
        std::string animName;
        bool loop = true;
        UAnimNodeSlot* slot = nullptr;
};

class Animation {
    public:
        bool& animPauseActive() {
            return animPauseActiveState;
        }

        void pauseAnimations(bool pause);
        void keepAnimationsPaused();
        void resetAnimation(const std::string& pawnName);
        void resetAllAnimations();
        void playAnimation(const std::string& pawnName, const std::string& animName, bool bLoop = true);

        // multi-actor playback, keyed by pawn name
        std::map<std::string, PlayedAnimState> played;

    private:
        bool animPauseActiveState = false;

        std::vector<UAnimNodeSequence*> pausedAnimNodes;
        std::vector<float> pausedAnimRates;
};

#endif // SAS_ANIMATION_H