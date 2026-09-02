#ifndef SAS_PCC_PARSER_H
#define SAS_PCC_PARSER_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

const uint32_t PCC_MAGIC_NUMBER = 0x9E2A83C1;
const uint32_t PCC_COMPRESSION_SCHEME = 0x400;

enum class PCCUnrealVersion : uint32_t {
    LE1 = 684,
    LE2 = 684,
    LE3 = 685
};
enum class PCCLicenseeVersion : uint32_t {
    LE1 = 171,
    LE2 = 168,
    LE3 = 205
};
enum class PCCPackageFlags : uint32_t {
    Compressed = 0x02000000U,
    FullyCompressed = 0x04000000U,
};

struct PCCImport {
        std::string packageName; // name[packageNameIndex]
        int32_t packageNumber = 0;
        std::string className; // name[classNameIndex]
        int32_t classNumber = 0;
        int32_t link = 0;       // Outer index in combined namespace
        std::string objectName; // name[objectNameIndex]
        int32_t objectNumber = 0;
};

struct PCCExport {
        // positive=export#N-1, negative=import#-N-1, 0=package root
        int32_t classIndex = 0;
        int32_t superIndex = 0;
        int32_t linkIndex = 0; // Outer: parent object
        int32_t nameIndex = 0; // index into name table
        int32_t nameNumber = 0;
        int32_t archetypeIndex = 0;
        uint64_t objectFlags = 0;
        int32_t dataSize = 0;
        int32_t dataOffset = 0;
        int32_t exportFlags = 0;
        int32_t generationsCount = 0;
        // resolved
        std::string className;  // resolved from classIndex
        std::string superName;  // resolved from superIndex
        std::string objectName; // resolved from nameIndex
        std::string fullName;   // built from parent chain
};

struct PCCFile {
        uint32_t magic = 0;
        uint16_t unrealVersion = 0;
        uint16_t licenseeVersion = 0;
        uint32_t packageFlags = 0;
        uint32_t compressionType = 0;

        std::vector<std::string> names;
        std::vector<PCCImport> imports;
        std::vector<PCCExport> exports;

        // object tree: children[i] = list of child-export indices for export i
        std::vector<std::vector<int32_t>> children;
        std::vector<int32_t> rootExports; // exports whose linkIndex == 0
};

// ── Parser ─────────────────────────────────────────────────────────────

class PCCParser {
    public:
        explicit PCCParser(const std::string& filePath);
        ~PCCParser();

        bool parse();

        // accessors
        const std::string& getFilePath() const {
            return filePath_;
        }
        bool isOpen() const {
            return fileStream_.is_open();
        }
        bool isParsed() const {
            return parsed_;
        }
        const PCCFile& fileData() const {
            return file_;
        }
        const std::vector<uint8_t>& decompressedBuffer() const {
            return decompressed_;
        }

    private:
        void readHeader();
        void decompress();
        void parseNameTable();
        void parseImportTable();
        void parseExportTable();
        void buildTree();

        std::string resolveName(int32_t index) const;              // 0-based index into name table
        std::string resolveObjectIndex(int32_t objectIndex) const; // combined namespace → name

        std::string filePath_;
        std::ifstream fileStream_;
        bool parsed_ = false;

        PCCFile file_;
        std::vector<uint8_t> raw_;
        std::vector<uint8_t> decompressed_;
};

#endif // SAS_PCC_PARSER_H
