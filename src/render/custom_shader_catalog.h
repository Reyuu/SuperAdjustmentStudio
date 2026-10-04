#ifndef SAS_CUSTOM_SHADER_CATALOG_H
#define SAS_CUSTOM_SHADER_CATALOG_H

#include <filesystem>
#include <string>
#include <vector>

struct CustomShaderCatalogEntry {
        std::string ref;         // relative to the shader folder (forward slashes), or absolute when outside it
        std::string displayName; // ref without the .hlsl suffix
        std::filesystem::path absPath;
};

// created on first call
const std::vector<std::filesystem::path>& customShaderRoots();
bool resolveCustomShaderRef(const std::string& ref, std::filesystem::path& outAbsPath);
std::string makeCustomShaderRef(const std::filesystem::path& absPath);
std::vector<CustomShaderCatalogEntry> scanCustomShaders();
bool resolveCustomShaderInclude(const std::filesystem::path& includingFile, const std::string& name, std::filesystem::path& outAbsPath, std::string& error);
void writeCustomShaderExamples();

#endif // SAS_CUSTOM_SHADER_CATALOG_H