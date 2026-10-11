#include "custom_shader_catalog.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>
#include <windows.h>

static const char* SHADER_EXTENSION = ".hlsl";

static const char* embeddedExampleShader() {
    return
#include "custom_shader_example_source.inc"
        ;
}

static const char* embeddedExampleShared() {
    return
#include "custom_shader_example_shared_source.inc"
        ;
}

// shipped shaders, seeded next to the examples
static const char* embeddedFilmGrain() {
    return
#include "custom_shader_filmgrain_source.inc"
        ;
}

static std::filesystem::path moduleDirectory() {
    HMODULE hModule = nullptr;
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&moduleDirectory),
                          &hModule) &&
        hModule != nullptr) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(hModule, path, MAX_PATH) != 0) {
            return std::filesystem::path(path).parent_path();
        }
    }
    return std::filesystem::current_path();
}

const std::vector<std::filesystem::path>& customShaderRoots() {
    static const std::vector<std::filesystem::path> roots = [] {
        std::vector<std::filesystem::path> out;
        out.push_back(moduleDirectory() / "SAS_Shaders");

        for (const auto& root : out) {
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
        }
        return out;
    }();
    return roots;
}

static std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return (char)::tolower(c);
    });
    return value;
}

static std::string lowerExtension(const std::filesystem::path& path) {
    return toLowerAscii(path.extension().string());
}

static bool pathComponentEquals(const std::filesystem::path& a, const std::filesystem::path& b) {
    const std::wstring left = a.native();
    const std::wstring right = b.native();
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) {
            return false;
        }
    }
    return true;
}

// component-wise containment, case-insensitive on Windows paths
static bool pathIsInside(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    auto candidateIt = candidate.begin();
    auto rootIt = root.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidate.end() || !pathComponentEquals(*candidateIt, *rootIt)) {
            return false;
        }
    }
    return true;
}

static std::filesystem::path canonicalOf(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return std::filesystem::path();
    }
    return canonical;
}

static int rootIndexFor(const std::filesystem::path& absPath) {
    const std::filesystem::path canonical = canonicalOf(absPath);
    if (canonical.empty()) {
        return -1;
    }
    const auto& roots = customShaderRoots();
    for (size_t i = 0; i < roots.size(); ++i) {
        if (pathIsInside(canonical, canonicalOf(roots[i]))) {
            return (int)i;
        }
    }
    return -1;
}

bool resolveCustomShaderRef(const std::string& ref, std::filesystem::path& outAbsPath) {
    if (ref.empty()) {
        return false;
    }

    const std::filesystem::path asPath = std::filesystem::path(ref).lexically_normal();
    if (asPath.is_absolute()) {
        outAbsPath = asPath;
        return true;
    }

    const auto& roots = customShaderRoots();
    for (size_t i = 0; i < roots.size(); ++i) {
        const std::filesystem::path candidate = std::filesystem::path(roots[i] / asPath).lexically_normal();
        if (!pathIsInside(candidate, std::filesystem::path(roots[i]).lexically_normal())) {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) {
            continue;
        }
        outAbsPath = candidate;
        return true;
    }
    return false;
}

std::string makeCustomShaderRef(const std::filesystem::path& absPath) {
    const std::filesystem::path normalized = absPath.lexically_normal();
    const std::filesystem::path canonical = canonicalOf(normalized);
    const std::filesystem::path comparable = canonical.empty() ? normalized : canonical;

    const auto& roots = customShaderRoots();
    for (const auto& root : roots) {
        const std::filesystem::path canonicalRoot = canonicalOf(root);
        if (canonicalRoot.empty() || !pathIsInside(comparable, canonicalRoot)) {
            continue;
        }
        const std::filesystem::path relative = comparable.lexically_relative(canonicalRoot);
        if (!relative.empty() && relative.native().front() != L'.') {
            return relative.generic_string();
        }
    }
    return comparable.generic_string();
}

std::vector<CustomShaderCatalogEntry> scanCustomShaders() {
    std::vector<CustomShaderCatalogEntry> out;
    const auto& roots = customShaderRoots();

    for (size_t i = 0; i < roots.size(); ++i) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator iterator(roots[i], std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && iterator != end; iterator.increment(ec)) {
            const std::filesystem::path path = iterator->path();
            if (lowerExtension(path) != SHADER_EXTENSION) {
                continue;
            }
            std::error_code fileEc;
            if (!iterator->is_regular_file(fileEc)) {
                continue;
            }

            CustomShaderCatalogEntry entry;
            entry.absPath = path.lexically_normal();
            entry.ref = makeCustomShaderRef(entry.absPath);
            entry.displayName = entry.ref;
            if (entry.displayName.size() >= std::string(SHADER_EXTENSION).size() &&
                toLowerAscii(entry.displayName.substr(entry.displayName.size() - std::string(SHADER_EXTENSION).size())) == SHADER_EXTENSION) {
                entry.displayName.erase(entry.displayName.size() - std::string(SHADER_EXTENSION).size());
            }
            out.push_back(std::move(entry));
        }
        if (ec) {
            Logger->warn("custom_shader: scan of '{}' stopped early: {}", roots[i].string(), ec.message());
        }
    }

    std::sort(out.begin(), out.end(), [](const CustomShaderCatalogEntry& a, const CustomShaderCatalogEntry& b) {
        const std::string left = toLowerAscii(a.displayName);
        const std::string right = toLowerAscii(b.displayName);
        if (left != right) {
            return left < right;
        }
        return a.displayName < b.displayName;
    });
    return out;
}

bool resolveCustomShaderInclude(const std::filesystem::path& includingFile, const std::string& name, std::filesystem::path& outAbsPath, std::string& error) {
    error.clear();
    if (name.empty()) {
        error = "empty include";
        return false;
    }

    const std::filesystem::path asPath(name);
    std::vector<std::filesystem::path> candidates;
    if (asPath.is_absolute()) {
        candidates.push_back(asPath);
    } else {
        candidates.push_back(includingFile.parent_path() / asPath);
        for (const auto& root : customShaderRoots()) {
            candidates.push_back(root / asPath);
        }
    }

    std::string escaped;
    for (const auto& candidate : candidates) {
        const std::filesystem::path canonical = canonicalOf(candidate);
        if (canonical.empty()) {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(canonical, ec)) {
            continue;
        }
        if (rootIndexFor(canonical) < 0) {
            // found, but outside the shader folder: remember why and keep
            // looking in case a later candidate stays inside the roots
            if (escaped.empty()) {
                escaped = "'" + name + "' resolves outside the shader folder: " + canonical.string();
            }
            continue;
        }
        outAbsPath = canonical;
        return true;
    }

    error = escaped.empty() ? ("cannot resolve #include \"" + name + "\"") : escaped;
    return false;
}

static bool writeIfMissing(const std::filesystem::path& path, const char* contents) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        Logger->warn("custom_shader: cannot write '{}'", path.string());
        return false;
    }
    out << contents;
    if (!out.good()) {
        Logger->warn("custom_shader: failed while writing '{}'", path.string());
        return false;
    }
    Logger->info("custom_shader: wrote starter shader '{}'", path.string());
    return true;
}

void writeCustomShaderExamples() {
    for (const auto& root : customShaderRoots()) {
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        writeIfMissing(root / "example.hlsl", embeddedExampleShader());
        writeIfMissing(root / "shared.hlsl", embeddedExampleShared());
        writeIfMissing(root / "filmgrain.hlsl", embeddedFilmGrain());
    }
}