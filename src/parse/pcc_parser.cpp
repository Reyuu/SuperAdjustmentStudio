#include "pcc_parser.h"
#include "logger.h"
#include "oodle.h"

#include <cstring>
#include <algorithm>

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

    readRawFile();
    if (raw_.empty()) {
        return false;
    }

    if (!readPreliminaryHeader()) {
        return false;
    }

    bool fullyCompressed = (file_.packageFlags & static_cast<uint32_t>(PCCPackageFlags::FullyCompressed)) != 0;

    if (fullyCompressed) {
        decompress();
        if (decompressed_.empty()) {
            return false;
        }
        readFullHeader();
    } else {
        readFullHeader();
        decompress();
        if (decompressed_.empty()) {
            return false;
        }
    }

    parseNameTable();
    parseImportTable();
    parseExportTable();
    buildTree();
    parsed_ = true;
    Logger->info("Parsed {} exports, {} imports from: {}", file_.exports.size(), file_.imports.size(), filePath_);
    return true;
}

static uint32_t readU32(const std::vector<uint8_t>& buf, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= buf.size()) {
        std::memcpy(&v, buf.data() + off, 4);
    }
    return v;
}

// folder name is a length-prefixed string
static size_t skipFolderName(const std::vector<uint8_t>& buf, size_t p) {
    constexpr size_t kMaxFolderLen = 512;
    if (p + 4 > buf.size()) {
        return buf.size();
    }
    int32_t folderLen = 0;
    std::memcpy(&folderLen, buf.data() + p, 4);
    p += 4;
    if (folderLen > 0) {
        if ((size_t)folderLen > kMaxFolderLen) {
            return buf.size();
        }
        p += (size_t)folderLen;
    } else if (folderLen < 0) {
        if ((size_t)-folderLen > kMaxFolderLen) {
            return buf.size();
        }
        p += (size_t)-folderLen * sizeof(wchar_t);
    }
    return p;
}

void PCCParser::readRawFile() {
    fileStream_.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(fileStream_.tellg());
    fileStream_.seekg(0, std::ios::beg);
    raw_.resize(fileSize);
    fileStream_.read(reinterpret_cast<char*>(raw_.data()), fileSize);
}

bool PCCParser::readPreliminaryHeader() {
    if (raw_.size() < 12) {
        Logger->error("File too small for header");
        return false;
    }
    file_.magic = readU32(raw_, 0x00);
    if (file_.magic != PCC_MAGIC_NUMBER) {
        Logger->error("Bad magic: {:#x}", file_.magic);
        raw_.clear();
        return false;
    }
    file_.unrealVersion = static_cast<uint16_t>(readU32(raw_, 0x04) & 0xFFFF);
    file_.licenseeVersion = static_cast<uint16_t>((readU32(raw_, 0x04) >> 16) & 0xFFFF);
    file_.headerSize = readU32(raw_, 0x08);

    size_t p = skipFolderName(raw_, 0x0C);
    compStartOffset_ = p;
    if (p + 4 <= raw_.size()) {
        file_.packageFlags = readU32(raw_, p);
    }

    Logger->info("readPreliminary: version={}/{} headerSize={} pkgFlags={:#x} compStart={:#x} rawSize={}", file_.unrealVersion, file_.licenseeVersion,
                 file_.headerSize, file_.packageFlags, compStartOffset_, raw_.size());
    return true;
}

void PCCParser::readFullHeader() {
    const auto& buf = decompressed_.empty() ? raw_ : decompressed_;
    if (buf.size() < 16) {
        return;
    }
    auto r32 = [&](size_t o) {
        return readU32(buf, o);
    };

    size_t p = skipFolderName(buf, 0x0C);
    if (p + 4 > buf.size()) {
        return;
    }

    file_.packageFlags = r32(p);
    p += 4;
    // ME3/LE3 only: extra int32 after PackageFlags when cooked (licensee >= 194).
    // LE2 (licensee 168) and LE1 (171) don't have it
    const bool isLE3Header = file_.licenseeVersion >= 194 || file_.unrealVersion == 685;
    if (isLE3Header) {
        p += 4;
    }
    file_.nameCount = r32(p);
    p += 4;
    file_.nameOffset = r32(p);
    p += 4;
    file_.exportCount = r32(p);
    p += 4;
    file_.exportOffset = r32(p);
    p += 4;
    file_.importCount = r32(p);
    p += 4;
    file_.importOffset = r32(p);
    p += 4;

    uint32_t depOff = r32(p);
    p += 4;
    uint32_t hdrOff = r32(p);
    p += 4;
    uint32_t depCount = r32(p);
    p += 4;
    uint32_t headersCount = r32(p);
    p += 4;
    p += 4;  // thumbnail table offset (unused in ME games, present in all LE headers)
    p += 16; // GUID
    uint32_t genCount = r32(p);
    p += 4;
    p += static_cast<size_t>(genCount) * 12; // generations
    uint32_t engineVer = r32(p);
    p += 4;
    uint32_t cookerVer = r32(p);
    p += 4;
    p += 8; // Bio unk3[2] (1, 0) — present in all LE headers (licensee >= 37)

    file_.compressionType = r32(p);
    p += 4;
    file_.compressedChunkCount = r32(p);
    p += 4;
    file_.compressedChunksOffset = p;

    Logger->info("readFullHeader: pkgFlags={:#x} le3Header={} names={} nameOff={} exports={} exportOff={} imports={} importOff={} engine={} cooker={} "
                 "compression={} chunks={} bufSize={}",
                 file_.packageFlags, isLE3Header, file_.nameCount, file_.nameOffset, file_.exportCount, file_.exportOffset, file_.importCount,
                 file_.importOffset, engineVer, cookerVer, file_.compressionType, file_.compressedChunkCount, buf.size());
}

void PCCParser::decompress() {
    if (raw_.empty()) {
        return;
    }
    if ((file_.packageFlags & static_cast<uint32_t>(PCCPackageFlags::FullyCompressed)) != 0) {
        decompressFullyCompressed();
        return;
    }
    decompressChunked();
}

void PCCParser::decompressFullyCompressed() {
    if (!Oodle_init()) {
        Logger->error("Oodle init failed for FullyCompressed");
        return;
    }
    size_t compStart = compStartOffset_;
    if (compStart >= raw_.size()) {
        Logger->error("FullyCompressed: compStart {:#x} past file end {}", compStart, raw_.size());
        return;
    }
    const uint8_t* compData = raw_.data() + compStart;
    size_t compLen = raw_.size() - compStart;
    size_t maxUncomp = compLen * 8;
    std::vector<uint8_t> uncompBuf(compStart + maxUncomp);
    std::memcpy(uncompBuf.data(), raw_.data(), compStart);
    size_t written = Oodle_decompress(compData, compLen, uncompBuf.data() + compStart, maxUncomp);
    if (written == 0) {
        Logger->error("FullyCompressed: Oodle decompress failed");
        return;
    }
    decompressed_.assign(uncompBuf.begin(), uncompBuf.begin() + compStart + written);
    Logger->info("FullyCompressed: {} -> {} bytes, total={}", compLen, written, decompressed_.size());
}

void PCCParser::decompressChunked() {
    if (file_.compressionType == 0) {
        decompressed_ = raw_;
        return;
    }
    if (file_.compressionType != PCC_COMPRESSION_SCHEME) {
        Logger->error("Unsupported compression: {}", file_.compressionType);
        return;
    }
    if (!Oodle_init()) {
        Logger->error("Oodle init failed");
        return;
    }

    constexpr uint32_t kBlockSize = 262144;
    constexpr uint32_t kMaxChunks = 4096;
    uint32_t chunkCount = file_.compressedChunkCount;
    size_t tableOff = file_.compressedChunksOffset;

    struct Chunk {
            int32_t uncompOffset, uncompSize, compOffset, compSize;
    };
    if (chunkCount > kMaxChunks || tableOff + static_cast<size_t>(chunkCount) * 16 > raw_.size()) {
        Logger->error("Chunk table out of bounds");
        return;
    }
    std::vector<Chunk> chunks(chunkCount);
    size_t maxEnd = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) {
        std::memcpy(&chunks[i], raw_.data() + tableOff + i * 16, 16);
        if (chunks[i].uncompOffset >= 0 && chunks[i].uncompSize > 0 && chunks[i].uncompSize < (1 << 30)) {
            maxEnd = (std::max)(maxEnd, static_cast<size_t>(chunks[i].uncompOffset) + static_cast<size_t>(chunks[i].uncompSize));
        }
    }
    if (maxEnd == 0 || maxEnd > (1u << 31)) {
        Logger->error("Invalid decompressed size");
        return;
    }
    decompressed_.resize(maxEnd);
    size_t rawHeaderEnd = tableOff + static_cast<size_t>(chunkCount) * 16;
    if (rawHeaderEnd > raw_.size()) {
        rawHeaderEnd = raw_.size();
    }
    if (rawHeaderEnd > maxEnd) {
        rawHeaderEnd = maxEnd;
    }
    std::memcpy(decompressed_.data(), raw_.data(), rawHeaderEnd);

    for (const auto& c : chunks) {
        if (c.uncompSize <= 0 || c.uncompOffset < 0 || c.compOffset < 0 || c.compSize <= 0) {
            continue;
        }
        size_t uOff = static_cast<size_t>(c.uncompOffset);
        size_t uSize = static_cast<size_t>(c.uncompSize);
        if (uOff + uSize > decompressed_.size()) {
            Logger->error("Chunk target out of bounds, skipping");
            continue;
        }
        uint32_t blockCount = (static_cast<uint32_t>(c.uncompSize) + kBlockSize - 1) / kBlockSize;
        size_t hdrs = 16 + static_cast<size_t>(blockCount) * 8;
        size_t dataOff = static_cast<size_t>(c.compOffset) + hdrs;
        if (dataOff >= raw_.size() || static_cast<size_t>(c.compSize) < hdrs || dataOff + (static_cast<size_t>(c.compSize) - hdrs) > raw_.size()) {
            Logger->error("Chunk source out of bounds, skipping");
            continue;
        }
        size_t dataLen = static_cast<size_t>(c.compSize) - hdrs;
        std::vector<uint8_t> tmp(static_cast<size_t>(c.uncompSize));
        size_t written = Oodle_decompress(raw_.data() + dataOff, dataLen, tmp.data(), tmp.size());
        if (written != static_cast<size_t>(c.uncompSize)) {
            Logger->error("Oodle decompress failed for chunk");
            decompressed_.clear();
            return;
        }
        std::memcpy(decompressed_.data() + static_cast<size_t>(c.uncompOffset), tmp.data(), written);
    }
}

static void appendUtf8(std::string& out, wchar_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

void PCCParser::parseNameTable() {
    constexpr int kMaxNameLen = 4096;
    // ME3 (licensee >= 142) stores no per-name
    // ME2 stores a 4-byte dword per entry
    size_t nameFlagsSize = 0;
    if (file_.licenseeVersion < 142) {
        nameFlagsSize = (file_.licenseeVersion >= 102) ? 4 : 8;
    }
    size_t pos = file_.nameOffset;
    file_.names.reserve(file_.nameCount);
    for (uint32_t i = 0; i < file_.nameCount; ++i) {
        if (pos + 4 > decompressed_.size()) {
            break;
        }
        int32_t nameLen = 0;
        std::memcpy(&nameLen, decompressed_.data() + pos, 4);
        pos += 4;
        if (nameLen == 0) {
            file_.names.emplace_back();
        } else if (nameLen < 0) {
            int wcount = -nameLen; // wchar count including null
            if (wcount > kMaxNameLen || pos + static_cast<size_t>(wcount) * 2 > decompressed_.size()) {
                break;
            }
            const wchar_t* w = reinterpret_cast<const wchar_t*>(decompressed_.data() + pos);
            std::string s;
            s.reserve(static_cast<size_t>(wcount));
            for (int k = 0; k < wcount && w[k] != 0; ++k) {
                appendUtf8(s, w[k]);
            }
            file_.names.push_back(std::move(s));
            pos += static_cast<size_t>(wcount) * 2;
        } else {
            if (nameLen > kMaxNameLen || pos + static_cast<size_t>(nameLen) > decompressed_.size()) {
                break;
            }
            file_.names.emplace_back(reinterpret_cast<const char*>(decompressed_.data() + pos), nameLen - 1);
            pos += nameLen;
        }
        if (nameFlagsSize > 0) {
            if (pos + nameFlagsSize > decompressed_.size()) {
                break;
            }
            pos += nameFlagsSize;
        }
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
    size_t pos = file_.importOffset;
    file_.imports.reserve(file_.importCount);
    for (uint32_t i = 0; i < file_.importCount; ++i) {
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
    size_t pos = file_.exportOffset;
    file_.exports.reserve(file_.exportCount);

    for (uint32_t i = 0; i < file_.exportCount; ++i) {
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
