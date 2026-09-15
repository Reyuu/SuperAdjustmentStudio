#ifndef SAS_LUT_CATALOG_H
#define SAS_LUT_CATALOG_H

#include <filesystem>
#include <string>
#include <vector>

// scan ASI/SAS_LUTs
// 1. Bare PNG (no matching .fx)
//      Layout #1: square Hald (width == height == n**3)
//      Layout #2: horizontal strip (width:height == 16:1)
// 2. MLUT:
//      Layout #1: (had to have matching .fx) from the corresponding MLUT definition
//      Each sub entry becomes its own LUT entry
// TODO: 3. Other LUT formats (e.g., cube files, 3D LUTs, .3dl files)
// else: unsupported formats

enum class LutEntryType {
    Hald,
    HorizontalStrip,
    MLUTSub,
    Error
};

struct LutCatalogEntry {
        // bare png
        std::string filename;
        std::string displayName;
        LutEntryType kind = LutEntryType::Error;

        // MLUT sub entries (if any)
        int subIndex = 0;
        int tileSize = 0;
        int tileAmount = 0;
        int lutAmount = 0;

        // Hald specific information (if any)
        int haldLevel = 0;

        // Horizontal strip specific information (if any)
        int stripTilesPerRow = 0;
        int stripSlice = 0;

        std::string error;
};

// layer reference persistance
struct LutRef {
        std::string file;
        int subIndex = -1;
};

struct MLUTMeta {
        std::string textureName;
        int tileSize = 0;
        int tileAmount = 0;
        int lutAmount = 0;
        std::vector<std::string> names;
};

bool parseLutRef(const std::string& ref, LutRef& outRef);
std::string makeLutRef(const std::string& file, int subIndex);

std::filesystem::path sasLutDirectory();
std::vector<LutCatalogEntry> scanSasLuts();

#endif // SAS_LUT_CATALOG_H