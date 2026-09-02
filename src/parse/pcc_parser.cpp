#include "pcc_parser.h"
#include "logger.h"
#include "oodle.h"

#include <cstring>

PCCParser::PCCParser(const std::string& filePath) : filePath_(filePath) {
    fileStream_.open(filePath, std::ios::binary);
    if (!fileStream_.is_open()) {
        Logger->error("Failed to open: " + filePath);
    }
}

PCCParser::~PCCParser() {
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

bool PCCParser::parse() {
    if (!fileStream_.is_open()) {
        return false;
    }
    readHeader();
    decompress();
    if (decompressed_.empty()) {
        return false;
    }
    parseNameTable();
    parseImportTable();
    parseExportTable();
    buildTree();
    parsed_ = true;
    Logger->info("Parsed " + std::to_string(file_.exports.size()) + " exports, " + std::to_string(file_.imports.size()) + " imports from: " + filePath_);
    return true;
}

void PCCParser::readHeader() {
    fileStream_.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(fileStream_.tellg());
    fileStream_.seekg(0, std::ios::beg);
    raw_.resize(fileSize);
    fileStream_.read(reinterpret_cast<char*>(raw_.data()), fileSize);

    auto r32 = [&](size_t o) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, raw_.data() + o, 4);
        return v;
    };
    auto r16 = [&](size_t o) -> uint16_t {
        uint16_t v;
        std::memcpy(&v, raw_.data() + o, 2);
        return v;
    };

    file_.magic = r32(0x00);
    if (file_.magic != PCC_MAGIC_NUMBER) {
        Logger->error("Bad magic: 0x" + std::to_string(file_.magic));
        raw_.clear();
        return;
    }
    file_.unrealVersion = r16(0x04);
    file_.licenseeVersion = r16(0x06);
    file_.packageFlags = r32(0x15);
    file_.compressionType = r32(0x75);
}

void PCCParser::decompress() {
    if (raw_.empty()) {
        return;
    }
    if (file_.compressionType == 0) {
        decompressed_ = raw_;
        return;
    }
    if (file_.compressionType != PCC_COMPRESSION_SCHEME) {
        Logger->error("Unsupported compression: " + std::to_string(file_.compressionType));
        return;
    }
    if (!Oodle_init()) {
        Logger->error("Oodle init failed");
        return;
    }

    const uint32_t BLOCK_SIZE = 262144;
    uint32_t chunkCount;
    std::memcpy(&chunkCount, raw_.data() + 0x79, 4);
    size_t tableOff = 0x7D;

    struct Chunk {
            int32_t uncompOffset, uncompSize, compOffset, compSize;
    };
    std::vector<Chunk> chunks(chunkCount);
    size_t maxEnd = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) {
        std::memcpy(&chunks[i], raw_.data() + tableOff + i * 16, 16);
        maxEnd = std::max<size_t>(maxEnd, static_cast<size_t>(chunks[i].uncompOffset + chunks[i].uncompSize));
    }
    decompressed_.resize(maxEnd);
    std::memcpy(decompressed_.data(), raw_.data(), 0x83);

    for (const auto& c : chunks) {
        if (c.uncompSize <= 0) {
            continue;
        }
        uint32_t blockCount = (static_cast<uint32_t>(c.uncompSize) + BLOCK_SIZE - 1) / BLOCK_SIZE;
        size_t hdrs = 16 + static_cast<size_t>(blockCount) * 8;
        size_t dataOff = static_cast<size_t>(c.compOffset) + hdrs;
        size_t dataLen = static_cast<size_t>(c.compSize) - hdrs;
        std::vector<uint8_t> tmp(static_cast<size_t>(c.uncompSize));
        size_t written = Oodle_decompress(raw_.data() + dataOff, dataLen, tmp.data(), tmp.size());
        if (written != static_cast<size_t>(c.uncompSize)) {
            Logger->error("Oodle decompress failed");
            decompressed_.clear();
            return;
        }
        std::memcpy(decompressed_.data() + static_cast<size_t>(c.uncompOffset), tmp.data(), written);
    }
}

void PCCParser::parseNameTable() {
    uint32_t nameCount, nameOff;
    std::memcpy(&nameCount, raw_.data() + 0x19, 4);
    std::memcpy(&nameOff, raw_.data() + 0x1D, 4);
    file_.names.reserve(nameCount);
    size_t pos = nameOff;
    for (uint32_t i = 0; i < nameCount; ++i) {
        if (pos + 4 > decompressed_.size()) {
            break;
        }
        int32_t nameLen;
        std::memcpy(&nameLen, decompressed_.data() + pos, 4);
        pos += 4;
        if (nameLen <= 0 || pos + static_cast<size_t>(nameLen) > decompressed_.size()) {
            break;
        }
        file_.names.emplace_back(reinterpret_cast<const char*>(decompressed_.data() + pos), nameLen - 1);
        pos += nameLen;
    }
}

std::string PCCParser::resolveName(int32_t index) const {
    if (index >= 0 && index < static_cast<int32_t>(file_.names.size())) {
        return file_.names[index];
    }
    return {};
}

// negative → import, positive → export, 0 → empty
std::string PCCParser::resolveObjectIndex(int32_t objectIndex) const {
    if (objectIndex < 0) {
        int32_t idx = -objectIndex - 1;
        if (idx >= 0 && idx < static_cast<int32_t>(file_.imports.size())) {
            return file_.imports[idx].className;
        }
    } else if (objectIndex > 0) {
        int32_t idx = objectIndex - 1;
        if (idx >= 0 && idx < static_cast<int32_t>(file_.exports.size())) {
            return file_.exports[idx].objectName;
        }
    }
    return {};
}

void PCCParser::parseImportTable() {
    uint32_t impCount, impOff;
    std::memcpy(&impCount, raw_.data() + 0x29, 4);
    std::memcpy(&impOff, raw_.data() + 0x2D, 4);
    file_.imports.reserve(impCount);
    size_t pos = impOff;
    for (uint32_t i = 0; i < impCount; ++i) {
        if (pos + 28 > decompressed_.size()) {
            break;
        }
        PCCImport ie;
        int32_t raw[7];
        std::memcpy(raw, decompressed_.data() + pos, 28);
        pos += 28;
        ie.packageName = resolveName(raw[0]);
        ie.packageNumber = raw[1];
        ie.className = resolveName(raw[2]);
        ie.classNumber = raw[3];
        ie.link = raw[4];
        ie.objectName = resolveName(raw[5]);
        ie.objectNumber = raw[6];
        file_.imports.push_back(std::move(ie));
    }
}

void PCCParser::parseExportTable() {
    uint32_t expCount, expOff;
    std::memcpy(&expCount, raw_.data() + 0x21, 4);
    std::memcpy(&expOff, raw_.data() + 0x25, 4);
    file_.exports.reserve(expCount);
    size_t pos = expOff;

    for (uint32_t i = 0; i < expCount; ++i) {
        if (pos + 40 > decompressed_.size()) {
            break;
        }
        PCCExport ex;
        int32_t raw[10];
        std::memcpy(raw, decompressed_.data() + pos, 40);
        pos += 40;
        ex.classIndex = raw[0];
        ex.superIndex = raw[1];
        ex.linkIndex = raw[2];
        ex.nameIndex = raw[3];
        ex.nameNumber = raw[4];
        ex.archetypeIndex = raw[5];
        std::memcpy(&ex.objectFlags, decompressed_.data() + pos - 40 + 24, 8);
        ex.dataSize = raw[8];
        ex.dataOffset = raw[9];

        // tail
        ex.exportFlags = 0;
        ex.generationsCount = 0;
        if (pos + 4 <= decompressed_.size()) {
            std::memcpy(&ex.exportFlags, decompressed_.data() + pos, 4);
            pos += 4;
        }
        if (pos + 4 <= decompressed_.size()) {
            std::memcpy(&ex.generationsCount, decompressed_.data() + pos, 4);
            pos += 4;
        }
        pos += static_cast<size_t>(ex.generationsCount) * 4; // generations
        pos += 16;                                           // GUID
        pos += 4;                                            // PackageFlags

        ex.objectName = resolveName(ex.nameIndex);
        // className/superName resolved in buildTree after all exports exist
        file_.exports.push_back(std::move(ex));
    }
}

void PCCParser::buildTree() {
    const int32_t N = static_cast<int32_t>(file_.exports.size());
    file_.children.resize(N);
    file_.rootExports.clear();

    // resolve className + superName now that all exports exist
    for (auto& ex : file_.exports) {
        ex.className = resolveObjectIndex(ex.classIndex);
        ex.superName = resolveObjectIndex(ex.superIndex);
    }

    // build parent→children and root list
    for (int32_t i = 0; i < N; ++i) {
        int32_t link = file_.exports[i].linkIndex;
        if (link == 0 || link > N || link < -static_cast<int32_t>(file_.imports.size())) {
            file_.rootExports.push_back(i);
        } else if (link > 0) {
            file_.children[link - 1].push_back(i);
        } else {
            // parent is an import → treat as root (import's children aren't exports here)
            file_.rootExports.push_back(i);
        }
    }

    // resolve full names (iterative with memoization)
    std::vector<std::string> names(N);
    std::vector<bool> done(N, false);
    std::function<std::string(int32_t)> resolve = [&](int32_t idx) -> std::string {
        if (done[idx]) {
            return names[idx];
        }
        const auto& ex = file_.exports[idx];
        if (ex.linkIndex == 0 || ex.linkIndex > N) {
            names[idx] = ex.objectName;
        } else if (ex.linkIndex > 0) {
            names[idx] = resolve(ex.linkIndex - 1) + "." + ex.objectName;
        } else {
            int32_t ii = -ex.linkIndex - 1;
            if (ii >= 0 && ii < static_cast<int32_t>(file_.imports.size())) {
                names[idx] = file_.imports[ii].objectName + "." + ex.objectName;
            } else {
                names[idx] = ex.objectName;
            }
        }
        done[idx] = true;
        return names[idx];
    };
    for (int32_t i = 0; i < N; ++i) {
        file_.exports[i].fullName = resolve(i);
    }
}