#ifndef SAS_PHOTO_OVERLAY_H
#define SAS_PHOTO_OVERLAY_H

#include <dxgi.h>
#include <d3d11.h>
#include <cmath>
#include <numbers>
#include "settings.h"

constexpr int PHOTO_OVERLAY_GRID_COUNT = 7;
typedef enum PhotoOverlayGrid {
    GRID_NONE,
    GRID_3X3,
    GRID_4X4,
    GRID_CROSSHAIR,
    GRID_DIAGONALS,
    GRID_PHI_GRID,
    GRID_GOLDEN_TRIANGLES
} PhotoOverlayGrid;
inline const char* gridLabels() {
    return "None\0"
           "3x3\0"
           "4x4\0"
           "Crosshair\0"
           "Diagonals\0"
           "Phi Grid\0"
           "Golden Triangles\0";
}
inline const char* gridName(int index) {
    switch (index) {
        case GRID_NONE:
            return "None";
        case GRID_3X3:
            return "3x3";
        case GRID_4X4:
            return "4x4";
        case GRID_CROSSHAIR:
            return "Crosshair";
        case GRID_DIAGONALS:
            return "Diagonals";
        case GRID_PHI_GRID:
            return "Phi Grid";
        case GRID_GOLDEN_TRIANGLES:
            return "Golden Triangles";
        default:
            return "Unknown";
    }
}

constexpr int ASPECT_RATIO_COUNT = 8;
typedef enum AspectRatio {
    ASPECT_RATIO_NONE,
    ASPECT_RATIO_16_9,
    ASPECT_RATIO_21_9,
    ASPECT_RATIO_1_1,
    ASPECT_RATIO_4_3,
    ASPECT_RATIO_5_4,
    ASPECT_RATIO_3_2,
    ASPECT_RATIO_9_16
} AspectRatio;
inline const char* aspectRatioLabels() {
    return "None\0"
           "16:9\0"
           "21:9\0"
           "1:1\0"
           "4:3\0"
           "5:4\0"
           "3:2\0"
           "9:16\0";
}
inline const char* aspectRatioName(int index) {
    switch (index) {
        case ASPECT_RATIO_NONE:
            return "None";
        case ASPECT_RATIO_16_9:
            return "16:9";
        case ASPECT_RATIO_21_9:
            return "21:9";
        case ASPECT_RATIO_1_1:
            return "1:1";
        case ASPECT_RATIO_4_3:
            return "4:3";
        case ASPECT_RATIO_5_4:
            return "5:4";
        case ASPECT_RATIO_3_2:
            return "3:2";
        case ASPECT_RATIO_9_16:
            return "9:16";
        default:
            return "Unknown";
    }
}
constexpr float ASPECT_RATIOS[][2] = {
    {0.0f,  0.0f }, // ASPECT_RATIO_NONE
    {16.0f, 9.0f }, // ASPECT_RATIO_16_9
    {21.0f, 9.0f }, // ASPECT_RATIO_21_9
    {1.0f,  1.0f }, // ASPECT_RATIO_1_1
    {4.0f,  3.0f }, // ASPECT_RATIO_4_3
    {5.0f,  4.0f }, // ASPECT_RATIO_5_4
    {3.0f,  2.0f }, // ASPECT_RATIO_3_2
    {9.0f,  16.0f}  // ASPECT_RATIO_9_16
};

constexpr float PHI_LO = 1.0f - (1.0f / std::numbers::phi_v<float>);
constexpr float PHI_HI = 1.0f / std::numbers::phi_v<float>;

constexpr int HIST_UPDATE_INTERVAL = 10;

class PhotoOverlay {
    public:
        bool& enabled() {
            return enabledState;
        }
        bool isEnabled() const {
            return enabledState;
        }
        bool isActive() const {
            return enabledState || filterState;
        }

        void render(ID3D11Device* device);
        void renderUi();
        void renderHistogram();
        void renderLutStackUi();
        void sample(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context);
        void shutdown();

    private:
        bool enabledState = SETTINGS_TOGGLE_OFF;
        PhotoOverlayGrid gridIndex = GRID_NONE;
        AspectRatio aspectRatio = ASPECT_RATIO_NONE;
        float maskOpacity = SETTINGS_PHOTO_MASK_OPACITY_DEFAULT;
        float lineColor[4] = {SETTINGS_COLOR_WHITE_R, SETTINGS_COLOR_WHITE_G, SETTINGS_COLOR_WHITE_B, SETTINGS_PHOTO_LINE_ALPHA_DEFAULT};
        float lineThickness = SETTINGS_PHOTO_LINE_THICKNESS_DEFAULT;
        bool centerDot = SETTINGS_TOGGLE_OFF;
        bool safeFrame = SETTINGS_TOGGLE_OFF;
        bool readout = SETTINGS_TOGGLE_OFF;

        bool noFogState = SETTINGS_TOGGLE_OFF;
        bool noLensFlareState = SETTINGS_TOGGLE_OFF;

        bool histogram = SETTINGS_TOGGLE_OFF;
        static constexpr int HIST_BINS = 64;
        static constexpr int HIST_EVERY = 10;
        float histR[HIST_BINS] = {};
        float histG[HIST_BINS] = {};
        float histB[HIST_BINS] = {};
        float histL[HIST_BINS] = {};
        int histTick = 0;
        float clipLo = 0.0f;
        float clipHi = 0.0f;
        int clipLoThr = SETTINGS_PHOTO_CLIP_LO_DEFAULT;
        int clipHiThr = SETTINGS_PHOTO_CLIP_HI_DEFAULT;
        ID3D11Texture2D* histogramTexture = nullptr;
        unsigned histW = 0;
        unsigned histH = 0;
        int histFmt = 0;

        bool filterState = SETTINGS_TOGGLE_OFF;
        float tintColor[3] = {SETTINGS_COLOR_WHITE_R, SETTINGS_COLOR_WHITE_G, SETTINGS_COLOR_WHITE_B};
        float tintStrength = SETTINGS_PHOTO_TINT_STRENGTH_DEFAULT;
        float grainIntensity = SETTINGS_PHOTO_GRAIN_INTENSITY_DEFAULT;
        float grainOpacity = SETTINGS_PHOTO_GRAIN_OPACITY_DEFAULT;
};

#endif // SAS_PHOTO_OVERLAY_H