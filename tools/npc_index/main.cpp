#include <iostream>
// builds the static NPC index by parsing every .pcc under BioGame and picking out
// the pawn classes it contains. The result is a generated C++ table so SAS can offer
// every character in the game, not only the ones the current level happened to load.
//
//   npc_index <BIOGame dir> <output .inc> [--verbose] [--confirm]
//   npc_index --dump <package.pcc> <nameFilter>     (read only, never writes)
//
// Oodle is loaded by DLL name, so run this with the game's Binaries/Win64 as the
// working directory (or on PATH).

#include "logger.h"
#include "pcc_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

ToolLogger gToolLogger;

std::string toLowerCase(std::string in) {
    std::transform(in.begin(), in.end(), in.begin(), [](unsigned char c) {
        return (char)tolower(c);
    });
    return in;
}

// authored display names, keyed by export name. anything not listed falls back to a
// name inferred from the export, so the index is still fully populated.
const std::map<std::string, std::string>& authoredNames() {
    static const std::map<std::string, std::string> names = {
        // the game ships numbered variants (Garrus_01 / Garrus_02); key on the real
        // export names so the readable names actually land
        {"SFXPawn_Garrus_01",          "Garrus Vakarian"            },
        {"SFXPawn_Garrus_02",          "Garrus Vakarian (variant 2)"},
        {"SFXPawn_Thane_01",           "Thane"                      },
        {"SFXPawn_Thane_02",           "Thane (variant 2)"          },
        {"SFXPawn_Kasumi_01",          "Kasumi Goto"                },
        {"SFXPawn_Jack_01",            "Jack (Subject Zero)"        },
        {"SFXPawn_Jack_02",            "Jack (variant 2)"           },
        {"SFXPawn_Miranda_01",         "Miranda Lawson"             },
        {"SFXPawn_Miranda_02",         "Miranda (variant 2)"        },
        {"SFXPawn_Mordin_01",          "Mordin Solus"               },
        {"SFXPawn_Tali_01",            "Tali'Zorah"                 },
        {"SFXPawn_Tali_02",            "Tali (variant 2)"           },
        {"SFXPawn_Jacob_01",           "Jacob Taylor"               },
        {"SFXPawn_Legion_01",          "Legion"                     },
        {"SFXPawn_Samara_01",          "Samara"                     },
        {"SFXPawn_Zaeed_01",           "Zaeed"                      },
        {"SFXPawn_Kenson_01",          "Kenson"                     },
        {"SFXPawn_Grunt_01",           "Cerberus grunt"             },
        {"SFXPawn_Grunt_02",           "Cerberus grunt (variant 2)" },
        {"SFXPawn_Henchman",           "Henchman (generic)"         },
        {"SFXPawn_Collector",          "Collector"                  },
        {"SFXPawn_VI_Core",            "Combat drone VI"            },
        {"SFXPawn_GunshipHock",        "Gunship"                    },
        {"SFXPawn_Cow_DLC",            "Cow"                        },
        {"SFXPawn_Monkey",             "Monkey"                     },
        {"SFXPawn_Oculus",             "Oculus"                     },
        {"SFXPawn_Scion",              "Scion"                      },
        {"SFXPawn_Vorcha",             "Vorcha"                     },
        {"SFXPawn_ShadowBrokerShield", "Shadow Broker's shield"     },
        {"SFXPawn_HeavyMech",          "Heavy mech"                 },
        {"SFXPawn_LightMech",          "Light mech"                 },
    };
    return names;
}

std::string inferName(const std::string& exportName) {
    std::string name = exportName;
    static const char* const prefixes[] = {"SFXPawn_", "BioPawn_", "BioPawn", "SFXPawn", "Pawn_", "Pawn"};
    for (const char* prefix : prefixes) {
        const size_t len = strlen(prefix);
        if (name.rfind(prefix, 0) == 0) {
            name = name.substr(len);
            break;
        }
    }
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_' || c == '-') {
            out += ' ';
            continue;
        }
        if (i > 0 && isupper((unsigned char)c) && !isupper((unsigned char)name[i - 1]) && name[i - 1] != ' ') {
            out += ' ';
        }
        out += c;
    }
    if (!out.empty()) {
        out[0] = (char)toupper((unsigned char)out[0]);
    }
    // drop a purely numeric trailing token: SFXPawn_Garrus_01 -> "Garrus", while
    // keeping meaningful ones such as _Legion or _DLC_00_Shared
    const size_t lastSpace = out.find_last_of(' ');
    if (lastSpace != std::string::npos && lastSpace + 1 < out.size()) {
        bool allDigits = true;
        for (size_t i = lastSpace + 1; i < out.size(); ++i) {
            if (!isdigit((unsigned char)out[i])) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            out.erase(lastSpace);
        }
    }
    return out.empty() ? exportName : out;
}

std::string escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// in a pcc a UClass export has classIndex 0, so the parser reports an empty
// className and "is this a Class" cannot be tested that way. the superclass chain
// does resolve, so a pawn class is one with a pawn ish superclass, or is a root.
bool looksLikePawnName(const std::string& name) {
    return name.rfind("SFXPawn", 0) == 0 || name.rfind("BioPawn", 0) == 0;
}

// BioWare ships its own patch content under a DLC_MOD_ prefixed folder, so those
// are kept even when mods are excluded
bool isOfficialPatchDlc(const std::string& dir) {
    return dir == "DLC_MOD_LE2PATCH" || dir == "DLC_MOD_LE3PATCH" || dir == "DLC_MOD_LE1PATCH";
}

bool isPawnSuper(const std::string& super) {
    return looksLikePawnName(super);
}

bool isPawnClassExport(const PCCExport& e) {
    if (e.objectName.rfind("Default__", 0) == 0) {
        return false; // a CDO instance, not the class
    }
    if (isPawnSuper(e.superName)) {
        return true;
    }
    // the roots: BioPawn's super is APawn, SFXPawn's is BioPawn (caught above)
    return e.objectName == "BioPawn" || e.objectName == "SFXPawn";
}

// ── read-only inspection mode, deliberately unable to reach the writer ─────────
// prints the tagged properties of the exports matching a name filter, to see what
// a game's data objects reference before building an index around them
int dumpProperties(const char* packagePath, const char* nameFilter) {
    PCCParser parser(packagePath);
    if (!parser.parse()) {
        std::fprintf(stderr, "parse failed\n");
        return 1;
    }
    const PCCFile& data = parser.fileData();
    std::fprintf(stderr, "%s: %zu exports, %zu imports\n", packagePath, data.exports.size(), data.imports.size());
    const std::string needle = toLowerCase(nameFilter);
    int shown = 0;
    for (size_t i = 0; i < data.exports.size(); ++i) {
        const PCCExport& e = data.exports[i];
        if (toLowerCase(e.objectName).find(needle) == std::string::npos) {
            continue;
        }
        std::vector<PCCProperty> props;
        if (!parser.readExportProperties(i, props)) {
            continue;
        }
        std::fprintf(stderr, "\n[%s]  class=[%s] super=[%s]\n", e.fullName.c_str(), e.className.c_str(), e.superName.c_str());
        for (const PCCProperty& p : props) {
            const std::string ref = parser.objectPropertyPath(p.valueOffset);
            std::fprintf(stderr, "  %-44s type=%-28s size=%d%s%s\n", p.name.c_str(), p.typeName.c_str(), p.size, ref.empty() ? "" : "  -> ", ref.c_str());
        }
        if (++shown >= 8) {
            std::fprintf(stderr, "(stopping after 8 matches)\n");
            break;
        }
    }
    return 0;
}

// dumps the package name table, every string the cooker recorded including the
// ones only property data references. this is how to see what the game itself
// calls things without guessing
int dumpNames(const char* packagePath, const char* nameFilter) {
    PCCParser parser(packagePath);
    if (!parser.parse()) {
        std::cerr << "parse failed" << std::endl;
        return 1;
    }
    const PCCFile& data = parser.fileData();
    const std::string needle = toLowerCase(nameFilter);
    std::cerr << packagePath << ": " << data.names.size() << " names, " << data.exports.size() << " exports" << std::endl;
    int shown = 0;
    for (const std::string& name : data.names) {
        if (needle != "*" && toLowerCase(name).find(needle) == std::string::npos) {
            continue;
        }
        ++shown;
        if (shown > 400) {
            std::cerr << "(truncated)" << std::endl;
            break;
        }
        std::cerr << "  name: " << name << std::endl;
    }
    if (needle == "*") {
        for (const PCCExport& e : data.exports) {
            std::cerr << "  export: " << e.objectName << "  class=[" << e.className << "]  full=[" << e.fullName << "]" << std::endl;
        }
    }
    return 0;
}

int dumpPackage(const char* packagePath, const char* nameFilter) {
    PCCParser parser(packagePath);
    if (!parser.parse()) {
        std::fprintf(stderr, "parse failed\n");
        return 1;
    }
    const PCCFile& data = parser.fileData();
    std::fprintf(stderr, "%s: %zu exports, %zu imports\n", packagePath, data.exports.size(), data.imports.size());
    const std::string needle = toLowerCase(nameFilter);
    int shown = 0;
    for (const PCCExport& e : data.exports) {
        if (toLowerCase(e.objectName).find(needle) == std::string::npos) {
            continue;
        }
        std::fprintf(stderr, "  obj=%-32s class=[%s] super=[%s] link=%d full=[%s]\n", e.objectName.c_str(), e.className.c_str(), e.superName.c_str(),
                     e.linkIndex, e.fullName.c_str());
        if (++shown > 40) {
            break;
        }
    }
    return 0;
}

// output safety: this tool writes into the source tree, so the target is checked
bool refuse(const std::string& why) {
    std::fprintf(stderr, "REFUSING TO WRITE: %s\n", why.c_str());
    return false;
}

// never let this tool overwrite something that already exists, and never let it
// write inside a game installation.
bool validateOutputPath(const std::filesystem::path& output, const std::filesystem::path& bioGame) {
    const std::string lower = toLowerCase(output.string());
    if (std::filesystem::exists(output)) {
        return refuse("the output file already exists: " + output.string());
    }
    if (toLowerCase(output.extension().string()) == ".pcc") {
        return refuse("the output path is a .pcc, which this tool must never write");
    }
    const std::string lowerBio = toLowerCase(bioGame.string());
    if (lowerBio.size() > 3 && lower.rfind(lowerBio, 0) == 0) {
        return refuse("the output path is inside the game directory " + bioGame.string());
    }
    if (lower.find("biogame") != std::string::npos) {
        return refuse("the output path contains 'BIOGame'");
    }
    const std::filesystem::path parent = output.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        return refuse("the output directory does not exist: " + parent.string());
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc >= 4 && strcmp(argv[1], "--dump") == 0) {
        return dumpPackage(argv[2], argv[3]);
    }
    if (argc >= 4 && strcmp(argv[1], "--props") == 0) {
        return dumpProperties(argv[2], argv[3]);
    }
    if (argc >= 4 && strcmp(argv[1], "--names") == 0) {
        return dumpNames(argv[2], argv[3]);
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: npc_index <BIOGame dir> <output .inc> [--verbose] [--confirm]\n");
        std::fprintf(stderr, "       npc_index --dump <package.pcc> <nameFilter>\n");
        std::fprintf(stderr, "       npc_index --props <package.pcc> <nameFilter>\n");
        return 2;
    }

    const std::filesystem::path bioGame = argv[1];
    const std::filesystem::path outPath = argv[2];
    bool confirmed = false;
    bool includeMods = false;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--include-mods") == 0) {
            includeMods = true;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            ToolLog::gVerbose = true;
        }
        if (strcmp(argv[i], "--confirm") == 0) {
            confirmed = true;
        }
    }

    if (!std::filesystem::is_directory(bioGame)) {
        std::fprintf(stderr, "input is not a directory: %s\n", bioGame.string().c_str());
        return 2;
    }
    if (!validateOutputPath(outPath, bioGame)) {
        return 3;
    }
    std::fprintf(stderr, "read from : %s\n", bioGame.string().c_str());
    std::fprintf(stderr, "write to  : %s\n", outPath.string().c_str());
    if (!confirmed) {
        std::fprintf(stderr, "re-run with --confirm to write.\n");
        return 4;
    }

    std::vector<std::filesystem::path> packages;
    auto addDir = [&packages](const std::filesystem::path& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) {
                continue;
            }
            if (entry.is_regular_file() && toLowerCase(entry.path().extension().string()) == ".pcc") {
                packages.push_back(entry.path());
            }
        }
    };
    addDir(bioGame / "CookedPCConsole");
    std::error_code ec;
    const std::filesystem::path dlcRoot = bioGame / "DLC";
    if (std::filesystem::exists(dlcRoot, ec)) {
        for (const auto& dlc : std::filesystem::directory_iterator(dlcRoot, ec)) {
            if (ec || !std::filesystem::is_directory(dlc, ec)) {
                continue;
            }
            addDir(dlc.path() / "CookedPCConsole");
        }
    }
    std::sort(packages.begin(), packages.end());
    std::fprintf(stderr, "scanning %zu packages from %s...\n", packages.size(), bioGame.string().c_str());

    // mod directories hold third-party content and are skipped unless asked for
    if (!includeMods) {
        std::vector<std::filesystem::path> filtered;
        for (const auto& p : packages) {
            const std::string dir = p.parent_path().parent_path().filename().string();
            if (dir.rfind("DLC_MOD_", 0) == 0 && !isOfficialPatchDlc(dir)) {
                continue;
            }
            filtered.push_back(p);
        }
        std::fprintf(stderr, "excluded %zu package(s) from mod directories (pass --include-mods to keep them)\n", packages.size() - filtered.size());
        packages = filtered;
    }

    std::vector<std::string> failures;
    struct Entry {
            std::string exportName;
            std::string packageName;   // runtime package, e.g. SFXGamePawnsDLC_CON_Pack02
            std::string sourcePackage; // the .pcc that holds it, used to open the file
            std::string displayName;
            std::string category; // authored name means a real character, rest is background
    };
    std::map<std::string, Entry> byExport;
    // LE1 has no per-character pawn classes, a character is a "hench_<name>"
    // package of Bio_Appr_Character_* settings applied to a generic player pawn
    std::map<std::string, Entry> characters;
    size_t parsed = 0;
    size_t failed = 0;

    for (const std::filesystem::path& path : packages) {
        PCCParser parser(path.string());
        if (!parser.parse()) {
            ++failed;
            failures.push_back(path.string());
            continue;
        }
        ++parsed;
        const PCCFile& data = parser.fileData();
        const std::string pccName = std::filesystem::path(path).stem().string();
        for (const PCCExport& e : data.exports) {
            const size_t groupDot = e.fullName.find('.');
            const std::string topGroup = groupDot == std::string::npos ? std::string() : e.fullName.substr(0, groupDot);
            if (topGroup.rfind("hench_", 0) == 0) {
                if (characters.find(topGroup) == characters.end()) {
                    characters[topGroup] = Entry{topGroup, topGroup, pccName, inferName(topGroup.substr(6)), "Characters"};
                }
            }
            if (!isPawnClassExport(e)) {
                continue;
            }
            // A level pcc holds two copies of a pawn class: one under the real
            // package group (SFXGamePawns...) and one under TheWorld.PersistentLevel.
            // only the former is the runtime class name, so skip level copies
            std::string prefix;
            const size_t dot = e.fullName.rfind('.');
            if (dot != std::string::npos) {
                prefix = e.fullName.substr(0, dot);
            }
            if (prefix.rfind("TheWorld", 0) == 0 || prefix.find("PersistentLevel") != std::string::npos) {
                continue;
            }
            if (prefix.empty()) {
                prefix = pccName;
            }

            auto existing = byExport.find(e.objectName);
            if (existing != byExport.end()) {
                // keep the shortest package group, which is the base one
                if (prefix.size() >= existing->second.packageName.size()) {
                    continue;
                }
            }
            auto authored = authoredNames().find(e.objectName);
            const bool isCharacter = authored != authoredNames().end();
            byExport[e.objectName] =
                Entry{e.objectName, prefix, pccName, isCharacter ? authored->second : inferName(e.objectName), isCharacter ? "Characters" : "Background NPCs"};
        }
    }

    std::fprintf(stderr, "parsed %zu packages (%zu failed), found %zu pawn classes and %zu character groups\n", parsed, failed, byExport.size(),
                 characters.size());

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", outPath.string().c_str());
        return 1;
    }
    out << "// Generated by tools/npc_index - do not edit by hand.\n";
    out << "// " << parsed << " packages parsed, " << byExport.size() << " pawn classes.\n";
    out << "struct StaticPawnIndexEntry {\n";
    out << "    const char* displayName;\n";
    out << "    const char* exportName;\n";
    out << "    const char* packageName;\n";
    out << "    const char* sourcePackage;\n";
    out << "    const char* category;\n";
    out << "};\n\n";
    out << "static const StaticPawnIndexEntry staticPawnIndex[] = {\n";
    for (const auto& item : byExport) {
        out << "    {\"" << escape(item.second.displayName) << "\", \"" << escape(item.second.exportName) << "\", \"" << escape(item.second.packageName)
            << "\", \"" << escape(item.second.sourcePackage) << "\", \"" << escape(item.second.category) << "\"},\n";
    }
    out << "};\n";
    out << "static const int staticPawnIndexCount = " << byExport.size() << ";\n";
    out << "\n";
    out << "struct StaticCharacterEntry {\n";
    out << "    const char* displayName;\n";
    out << "    const char* groupName;\n";
    out << "    const char* sourcePackage;\n";
    out << "};\n\n";
    out << "static const StaticCharacterEntry staticCharacterIndex[] = {\n";
    for (const auto& item : characters) {
        out << "    {\"" << escape(item.second.displayName) << "\", \"" << escape(item.second.exportName) << "\", \"" << escape(item.second.sourcePackage)
            << "\"},\n";
    }
    out << "};\n";
    out << "static const int staticCharacterIndexCount = " << characters.size() << ";\n";
    out.close();
    std::fprintf(stderr, "wrote %s\n", outPath.string().c_str());
    return 0;
}
