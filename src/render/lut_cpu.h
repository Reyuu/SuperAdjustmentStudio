#ifndef SAS_LUT_CPU_H
#define SAS_LUT_CPU_H

// to bake the LUT onto the screenshots, we have to do it on the CPU
#include "lut_catalog.h"
#include "lut_depth.h"
#include "settings.h"

#include <memory>
#include <vector>
#include <string>

struct LutCpuLayer {
        bool valid = false;
        LutEntryType kind = LutEntryType::Error;
        LutLayerOptions options;
        std::shared_ptr<std::vector<unsigned char>> pixels;
        int w = 0;
        int h = 0;
        int level = 0;
        int tilesPerRow = 0;
        int tileSize = 0;
        int tileAmount = 0;
        int lutAmount = 0;
        int subIndex = 0;
};

struct CpuImage {
        const unsigned char* d = nullptr;
        int w = 0;
        int h = 0;
};

void applyLutCpuLayer(std::vector<LutCpuLayer>& layers, unsigned char* rgba, int w, int h, const CpuDepth& depth, bool invertDepth);

#endif // SAS_LUT_CPU_H