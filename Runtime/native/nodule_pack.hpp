#pragma once

#include <filesystem>
#include <string>

namespace noqeri::nodule {

struct PackageInfo {
    std::string coordinate;
    std::string version;
    std::string entry;
    std::string manifest;
};

PackageInfo pack(const std::filesystem::path& root,
                 const std::filesystem::path& output);

int publish(const std::filesystem::path& root,
            const std::filesystem::path& output,
            const std::string& registry_url,
            const std::string& token);

} // namespace noqeri::nodule
