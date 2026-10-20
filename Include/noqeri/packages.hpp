#pragma once
#include "diagnostics.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace noe {

enum class DependencySource { Registry, Git, Path };
struct DependencySpec { std::string package; std::string version; DependencySource source=DependencySource::Registry; std::string location; std::string checksum; };
struct ProjectManifest { std::string name="app",version="0.0.0",edition="2026",entry="src/main.noqeri",profile="app",target="x86_64-unknown-none",registry="https://github.com/EMN90909/noqeri-registry"; std::unordered_map<std::string,std::string> dependencies; std::unordered_map<std::string,DependencySpec> dependencySpecs; };
class PackageManager { public: std::optional<ProjectManifest> loadManifest(const std::filesystem::path& path,Diagnostics& diagnostics) const; bool writeLock(const ProjectManifest& manifest,const std::filesystem::path& path,Diagnostics& diagnostics) const; bool createProject(const std::filesystem::path& directory,const std::string& name,Diagnostics& diagnostics) const; };

} // namespace noe
