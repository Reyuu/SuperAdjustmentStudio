#include "lut_catalog.h"
#include "logger.h"
#include "util.h"
#include "translation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <windows.h>

#include "stb_image.h"

static bool isStringEqualish(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

// integer cube-root test: returns k when n == k^3, else 0
static int intCubeRootTest(int n) {
    if (n <= 0) {
        return 0;
    }
    const long long k = std::lround(std::cbrt((double)n));
    if (k <= 0 || k * k * k != n) {
        return 0;
    }
    return (int)k;
}

std::filesystem::path sasLutDirectory() {
    HMODULE hModule = nullptr;

    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&sasLutDirectory),
                          &hModule) &&
        hModule != nullptr) {

        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(hModule, path, MAX_PATH) != 0) {
            std::filesystem::path modulePath(path);
            return std::filesystem::path(path).parent_path() / "SAS_LUTs";
        }
    }
    // fallback to current directory
    return std::filesystem::current_path() / "SAS_LUTs";
}

// parse a LUT reference string into a LutRef structure
// eg. "example.png#2" would result in outRef.file = "example.png" and outRef.subIndex = 2
bool parseLutRef(const std::string& ref, LutRef& outRef) {
    const size_t hash = ref.rfind('#');
    if (hash != std::string::npos && hash + 1 < ref.size()) {
        try {
            outRef.subIndex = std::stoi(ref.substr(hash + 1));
        } catch (...) {
            outRef.file = ref;
            outRef.subIndex = -1;
            return true;
        }
        outRef.file = ref.substr(0, hash);
        return true;
    }
    outRef.file = ref;
    outRef.subIndex = -1;
    return true;
}

std::string makeLutRef(const std::string& file, int subIndex) {
    if (subIndex < 0) {
        return file;
    }
    return file + "#" + std::to_string(subIndex);
}

static bool parseMLUTFx(const std::filesystem::path& fxPath, MLUTMeta& outMeta) {
    std::ifstream in(fxPath);
    if (!in.is_open()) {
        return false;
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    auto grab = [&](const char* key, std::string& outValue) {
        std::regex re(std::string("#define\\s+") + key + "\\s+(.+)");
        std::smatch match;
        if (!std::regex_search(content, match, re)) {
            return false;
        }
        outValue = match[1].str();

        const size_t a = outValue.find_first_not_of(" \t\r\n\"");
        const size_t b = outValue.find_last_not_of(" \t\r\n\"");
        if (a == std::string::npos) {
            outValue.clear();
            return false;
        }
        outValue = outValue.substr(a, b - a + 1);
        return true;
    };

    std::string v;
    if (!grab("fLUT_TextureName", outMeta.textureName)) {
        return false;
    }
    if (!grab("fLUT_TileSizeXY", v)) {
        return false;
    }
    outMeta.tileSize = std::atoi(v.c_str());

    if (!grab("fLUT_TileAmount", v)) {
        return false;
    }
    outMeta.tileAmount = std::atoi(v.c_str());

    if (!grab("fLUT_LutAmount", v)) {
        return false;
    }
    outMeta.lutAmount = std::atoi(v.c_str());

    if (outMeta.tileSize <= 0 || outMeta.tileAmount <= 0 || outMeta.lutAmount <= 0) {
        return false;
    }

    std::string list;
    if (!grab("fLUT_LutList", list)) {
        return false;
    }
    outMeta.names.clear();

    {
        std::string pattern("#define\\s+fLUT_LutList\\s+\"(.*)\"\\s*$");
        std::regex re(pattern, std::regex_constants::multiline);
        std::smatch m;
        if (std::regex_search(content, m, re)) {
            std::string raw = m[1].str();
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t end = raw.find("\\0", pos);
                // move the position to the next string
                std::string name = raw.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                const size_t a = name.find_first_not_of(" \t");
                const size_t b = name.find_last_not_of(" \t");
                if (a != std::string::npos) {
                    outMeta.names.push_back(name.substr(a, b - a + 1));
                }
                if (end == std::string::npos) {
                    break;
                }
                // move past the two-char \0 escape to the start of the next string
                pos = end + 2;
            }
        }
    }

    if ((int)outMeta.names.size() != outMeta.lutAmount) {
        // names are display only, trim
        while ((int)outMeta.names.size() < outMeta.lutAmount) {
            outMeta.names.push_back("LUT " + std::to_string(outMeta.names.size() + 1));
        }
        outMeta.names.resize(outMeta.lutAmount);
    }

    return true;
}

std::vector<LutCatalogEntry> scanSasLuts() {
    std::vector<LutCatalogEntry> out;
    const std::filesystem::path dir = sasLutDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        return out;
    }

    std::vector<std::filesystem::path> pngs;
    std::vector<std::filesystem::path> fxs;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png") {
            pngs.push_back(entry.path());
        } else if (ext == ".fx") {
            fxs.push_back(entry.path());
        }
    }

    auto findFxFor = [&](const std::string& pngName) -> std::filesystem::path {
        const std::string stem = std::filesystem::path(pngName).stem().string();
        for (const auto& fx : fxs) {
            if (isStringEqualish(fx.stem().string(), stem)) {
                return fx;
            }
        }
        return {};
    };

    auto makeEntry = [](const std::string& filename, const std::string& displayName, LutEntryType kind, std::string error = {}) {
        LutCatalogEntry entry;
        entry.filename = filename;
        entry.displayName = displayName;
        entry.kind = kind;
        entry.error = std::move(error);
        return entry;
    };

    for (const auto& png : pngs) {
        const std::string bare = png.filename().string();
        const std::filesystem::path fx = findFxFor(bare);
        if (!fx.empty()) {
            MLUTMeta meta;
            if (!parseMLUTFx(fx, meta)) {
                out.push_back(makeEntry(bare, bare, LutEntryType::Error, t("ui.lut_catalog.failed_parse_mlutfx")));
                continue;
            }
            if (!isStringEqualish(meta.textureName, bare)) {
                out.push_back(
                    makeEntry(bare, bare, LutEntryType::Error, "Texture name does not match PNG filename " + bare + " (expected " + meta.textureName + ")"));
                continue;
            }

            int w = 0;
            int h = 0;
            int comp = 0;
            if (!stbi_info(png.string().c_str(), &w, &h, &comp) || w != meta.tileSize * meta.tileAmount || h != meta.tileSize * meta.lutAmount) {
                out.push_back(makeEntry(bare, bare, LutEntryType::Error, t("ui.lut_catalog.atlas_size_mismatch")));
                continue;
            }

            const std::string packLabel = png.stem().string();
            for (int i = 0; i < meta.lutAmount; ++i) {
                std::string subName = "LUT " + std::to_string(i + 1);
                if ((size_t)i < meta.names.size() && !meta.names[(size_t)i].empty()) {
                    subName = meta.names[(size_t)i];
                }
                LutCatalogEntry entry = makeEntry(bare, packLabel + " - " + subName, LutEntryType::MLUTSub);
                entry.subIndex = i;
                entry.tileSize = meta.tileSize;
                entry.tileAmount = meta.tileAmount;
                entry.lutAmount = meta.lutAmount;
                out.push_back(entry);
            }
            continue;
        }

        int w = 0;
        int h = 0;
        int comp = 0;
        if (!stbi_info(png.string().c_str(), &w, &h, &comp) || w <= 0 || h <= 0) {
            out.push_back(makeEntry(bare, bare, LutEntryType::Error, t("ui.lut_catalog.failed_read_png")));
            continue;
        }

        const int haldLevel = intCubeRootTest(w);
        if (w == h && haldLevel != 0) {
            LutCatalogEntry entry = makeEntry(bare, bare, LutEntryType::Hald);
            entry.haldLevel = haldLevel;
            out.push_back(entry);
            continue;
        }

        {
            const int total = w * h;
            const int n = intCubeRootTest(total);
            if (n > 0 && w % n == 0 && h % n == 0 && (w / n) * (h / n) == n) {
                LutCatalogEntry entry = makeEntry(bare, bare, LutEntryType::HorizontalStrip);
                entry.stripSlice = n;
                entry.stripTilesPerRow = w / n;
                out.push_back(entry);
                continue;
            }
        }

        out.push_back(makeEntry(bare, bare, LutEntryType::Error, t("ui.lut_catalog.unrecognized_lut_format")));
    }

    std::sort(out.begin(), out.end(), [](const LutCatalogEntry& a, const LutCatalogEntry& b) {
        return a.displayName < b.displayName;
    });
    Logger->debug("lut: scanned {} entries in {}", out.size(), dir.string());
    return out;
}