#include "lut_cpu.h"

#include <algorithm>
#include <cmath>

// mirror hlsl shader but on CPU

// clamp a value between 0 and 1
static inline float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// sample a CPU-backed RGBA image using bilinear filtering at normalized UV coordinates.
static void sampleBilinear(const CpuImage& image, float u, float v, float out[3]) {
    const float clampedU = std::clamp(u, 0.0f, static_cast<float>(image.w) - 1.0f);
    const float clampedV = std::clamp(v, 0.0f, static_cast<float>(image.h) - 1.0f);

    const int x0 = static_cast<int>(clampedU);
    const int y0 = static_cast<int>(clampedV);
    const int x1 = x0 + 1 < image.w ? x0 + 1 : image.w - 1;
    const int y1 = y0 + 1 < image.h ? y0 + 1 : image.h - 1;

    const float xBlend = clampedU - static_cast<float>(x0);
    const float yBlend = clampedV - static_cast<float>(y0);

    const unsigned char* topLeft = image.d + ((size_t)y0 * image.w + x0) * 4;
    const unsigned char* topRight = image.d + ((size_t)y0 * image.w + x1) * 4;
    const unsigned char* bottomLeft = image.d + ((size_t)y1 * image.w + x0) * 4;
    const unsigned char* bottomRight = image.d + ((size_t)y1 * image.w + x1) * 4;

    for (int channel = 0; channel < 3; ++channel) {
        // stb loads RGBA
        // D3D R8G8B8A8 CPU pixels are R,G,B,A order too.
        const float topSample = (topLeft[channel] * (1.0f - xBlend) + topRight[channel] * xBlend) / 255.0f;
        const float bottomSample = (bottomLeft[channel] * (1.0f - xBlend) + bottomRight[channel] * xBlend) / 255.0f;
        out[channel] = topSample * (1.0f - yBlend) + bottomSample * yBlend;
    }
}

// fetch a value from a HALD LUT cube. level N means an N^3 x N^3 image holding
// an N^2 x N^2 x N^2 color cube: N x N tiles of N^2 x N^2 pixels each.
static void sampleHaldLookupCpu(const CpuImage& image, float red, float green, float slice, int tilesPerRow, int tileSize, float out[3]) {
    const int sliceX = static_cast<int>(slice) % tilesPerRow;
    const int sliceY = static_cast<int>(slice) / tilesPerRow;
    sampleBilinear(image, sliceX * tileSize + red * (tileSize - 1), sliceY * tileSize + green * (tileSize - 1), out);
}

// fetch a value from a strip-based LUT where each slice is a tile grid.
static void sampleStripLookupCpu(const CpuImage& image, float red, float green, float slice, int tilesPerRow, float out[3]) {
    const float tileWidth = static_cast<float>(image.w) / static_cast<float>(tilesPerRow);
    const int tileX = static_cast<int>(slice) % tilesPerRow;
    const int tileY = static_cast<int>(slice) / tilesPerRow;
    sampleBilinear(image, tileX * tileWidth + red * (tileWidth - 1.0f), tileY * tileWidth + green * (tileWidth - 1.0f), out);
}

// fetch a value from a LUT layer, handling Hald, Strip, and MLUT atlas types.
static void sampleLutCpu(const LutCpuLayer& layer, float red, float green, float blue, float out[3]) {
    CpuImage image{layer.pixels->data(), layer.w, layer.h};
    red = clamp01(red);
    green = clamp01(green);
    blue = clamp01(blue);

    if (layer.kind == LutEntryType::Hald) {
        if (layer.level < 2) {
            out[0] = red;
            out[1] = green;
            out[2] = blue;
            return;
        }
        const int tilesPerRow = layer.level;
        const int tileSize = layer.level * layer.level;
        const float sliceCount = static_cast<float>(tilesPerRow * tileSize);
        const float slicePosition = blue * (sliceCount - 1.0f);
        const float sliceIndex0 = std::floor(slicePosition);
        const float sliceBlend = slicePosition - sliceIndex0;

        float previousSliceSample[3];
        float nextSliceSample[3];
        sampleHaldLookupCpu(image, red, green, sliceIndex0, tilesPerRow, tileSize, previousSliceSample);
        float nextSliceIndex = sliceCount - 1.0f;
        if (sliceIndex0 + 1.0f < sliceCount - 1.0f) {
            nextSliceIndex = sliceIndex0 + 1.0f;
        }
        sampleHaldLookupCpu(image, red, green, nextSliceIndex, tilesPerRow, tileSize, nextSliceSample);

        for (int channel = 0; channel < 3; ++channel) {
            out[channel] = previousSliceSample[channel] + (nextSliceSample[channel] - previousSliceSample[channel]) * sliceBlend;
        }
        return;
    }

    if (layer.kind == LutEntryType::HorizontalStrip) {
        const float sliceCount = static_cast<float>(layer.level);
        const float slicePosition = blue * (sliceCount - 1.0f);
        const float sliceIndex0 = std::floor(slicePosition);
        const float sliceBlend = slicePosition - sliceIndex0;

        float previousSliceSample[3];
        float nextSliceSample[3];
        sampleStripLookupCpu(image, red, green, sliceIndex0, layer.tilesPerRow, previousSliceSample);
        float nextSliceIndex = sliceCount - 1.0f;
        if (sliceIndex0 + 1.0f < sliceCount - 1.0f) {
            nextSliceIndex = sliceIndex0 + 1.0f;
        }
        sampleStripLookupCpu(image, red, green, nextSliceIndex, layer.tilesPerRow, nextSliceSample);

        for (int channel = 0; channel < 3; ++channel) {
            out[channel] = previousSliceSample[channel] + (nextSliceSample[channel] - previousSliceSample[channel]) * sliceBlend;
        }
        return;
    }

    // MLUT atlas (yoinked from _BaseLUT.fxh)
    const float tileSize = static_cast<float>(layer.tileSize);
    const float tileAmount = static_cast<float>(layer.tileAmount);
    const float lutAmount = static_cast<float>(layer.lutAmount);
    const float texelXStep = 1.0f / tileSize / tileAmount;
    const float texelYStep = 1.0f / tileSize;

    float atlasX = (red * tileSize - red + 0.5f) * texelXStep;
    float atlasY = (green * tileSize - green + 0.5f) * texelYStep;
    float atlasZ = blue * tileSize - blue;

    atlasY /= lutAmount;
    atlasY += static_cast<float>(layer.subIndex) / lutAmount;

    const float sliceBlend = atlasZ - std::floor(atlasZ);
    atlasX += (atlasZ - sliceBlend) / tileAmount;

    float previousSliceSample[3];
    float nextSliceSample[3];
    sampleBilinear(image, atlasX * layer.w, atlasY * layer.h, previousSliceSample);
    sampleBilinear(image, (atlasX + texelYStep) * layer.w, atlasY * layer.h, nextSliceSample);

    for (int channel = 0; channel < 3; ++channel) {
        out[channel] = previousSliceSample[channel] + (nextSliceSample[channel] - previousSliceSample[channel]) * sliceBlend;
    }
}

void applyLutCpuLayer(std::vector<LutCpuLayer>& layers, unsigned char* rgba, int width, int height, const CpuDepth& depth, bool invertDepth) {
    static const float luminanceWeights[3] = {0.2125f, 0.7154f, 0.0721f};

    const auto saturate = [](float value) {
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    };

    const auto smoothStep = [&](float edge0, float edge1, float value) {
        float t = (value - edge0) / (edge1 - edge0);
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        return t * t * (3.0f - 2.0f * t);
    };

    const auto sampleDepthAt = [&](float u, float v) {
        if (depth.v.empty() || depth.w == 0 || depth.h == 0) {
            return 1.0f; // no capture yet: gate fully open (bake as graded)
        }

        const float x = u * (depth.w - 1);
        const float y = v * (depth.h - 1);
        const int x0 = static_cast<int>(x);
        const int y0 = static_cast<int>(y);
        const int x1 = x0 + 1 < depth.w ? x0 + 1 : depth.w - 1;
        const int y1 = y0 + 1 < depth.h ? y0 + 1 : depth.h - 1;
        const float xBlend = x - static_cast<float>(x0);
        const float yBlend = y - static_cast<float>(y0);

        const float* depthPixels = depth.v.data();
        const float topSample = depthPixels[(size_t)y0 * depth.w + x0] * (1.0f - xBlend) + depthPixels[(size_t)y0 * depth.w + x1] * xBlend;
        const float bottomSample = depthPixels[(size_t)y1 * depth.w + x0] * (1.0f - xBlend) + depthPixels[(size_t)y1 * depth.w + x1] * xBlend;
        float depthValue = topSample * (1.0f - yBlend) + bottomSample * yBlend;

        if (depth.linearize) {
            const float linearDepth = depth.nearZ * depth.farZ / (depth.farZ - depthValue * (depth.farZ - depth.nearZ) + 1e-6f);
            depthValue = saturate((linearDepth - depth.nearZ) / (depth.farZ - depth.nearZ + 1e-6f));
        }

        if (invertDepth) {
            depthValue = 1.0f - depthValue;
        }

        return depthValue;
    };

    for (const auto& layer : layers) {
        if (!layer.valid) {
            continue;
        }

        for (int y = 0; y < height; ++y) {
            unsigned char* row = rgba + (size_t)y * width * 4;
            const float normalizedY = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;

            for (int x = 0; x < width; ++x) {
                unsigned char* pixel = row + (size_t)x * 4;

                const float sourceRed = pixel[0] / 255.0f;
                const float sourceGreen = pixel[1] / 255.0f;
                const float sourceBlue = pixel[2] / 255.0f;

                float lutSample[3];
                sampleLutCpu(layer, sourceRed, sourceGreen, sourceBlue, lutSample);

                const float lutLuma = lutSample[0] * luminanceWeights[0] + lutSample[1] * luminanceWeights[1] + lutSample[2] * luminanceWeights[2];
                const float sourceLuma = sourceRed * luminanceWeights[0] + sourceGreen * luminanceWeights[1] + sourceBlue * luminanceWeights[2];

                float color[3];
                const float sourceColor[3] = {sourceRed, sourceGreen, sourceBlue};
                for (int channel = 0; channel < 3; ++channel) {
                    color[channel] = lutSample[channel] + (sourceColor[channel] - lutSample[channel]) * (1.0f - layer.options.chroma);
                }

                const float colorLuma = color[0] * luminanceWeights[0] + color[1] * luminanceWeights[1] + color[2] * luminanceWeights[2];
                const float targetLuma = lutLuma + (sourceLuma - lutLuma) * (1.0f - layer.options.luma);

                for (int channel = 0; channel < 3; ++channel) {
                    color[channel] = color[channel] - colorLuma + targetLuma;
                    color[channel] = color[channel] + (sourceColor[channel] - color[channel]) * (1.0f - layer.options.blend);
                }

                if (layer.options.useDepth) {
                    const float normalizedX = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
                    const float depthValue = sampleDepthAt(normalizedX, normalizedY);
                    const float focus = layer.options.depthFocus;
                    const float range = layer.options.depthRange < 1e-4f ? 1e-4f : layer.options.depthRange;
                    const float lowerEdge = saturate(focus - range);
                    const float upperEdge = saturate(focus + range);
                    const float softStart = lowerEdge < focus - 1e-4f ? lowerEdge : focus - 1e-4f;
                    const float softEnd = upperEdge > focus + 1e-4f ? upperEdge : focus + 1e-4f;
                    const float nearWeight = smoothStep(softStart, focus, depthValue);
                    const float farWeight = 1.0f - smoothStep(focus, softEnd, depthValue);
                    const float depthBlend = saturate(nearWeight * farWeight);
                    const float span = layer.options.blendMax - layer.options.blendMin;

                    float blendWeight = 0.0f;
                    if (span > 1e-4f) {
                        blendWeight = saturate((depthBlend - layer.options.blendMin) / span);
                    } else {
                        blendWeight = depthBlend >= layer.options.blendMin ? 1.0f : 0.0f;
                    }

                    for (int channel = 0; channel < 3; ++channel) {
                        color[channel] = sourceColor[channel] + (color[channel] - sourceColor[channel]) * blendWeight;
                    }
                }

                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<unsigned char>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
    }
}