#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace noqeri_nodule {

struct NdlDependency {
    std::string name;
    std::string version;
    std::string sha256Hex;
};

struct NdlManifest {
    std::string name;
    std::string version;
    std::string languageVersion = "0.1.5.1";
    std::uint32_t abiVersion = 2;
    std::string entry;
    std::vector<std::string> exports;
    std::vector<NdlDependency> dependencies;
};

struct NdlEntry {
    std::string path;
    std::string data;
    std::uint32_t adler32 = 0;
    std::string sha256Hex;
};

struct NdlArchive {
    NdlManifest manifest;
    std::vector<NdlEntry> entries;
    std::string archiveSha256Hex;
    const NdlEntry* findEntry(std::string_view path) const;
};

std::string buildNdlArchive(const NdlManifest& manifest,
                            const std::vector<std::pair<std::string, std::string>>& files);
bool loadNdlArchiveFromBytes(std::string_view bytes, NdlArchive& out, std::string& error);
bool loadNdlArchiveFromFile(const std::filesystem::path& path, NdlArchive& out, std::string& error);

} // namespace noqeri_nodule
