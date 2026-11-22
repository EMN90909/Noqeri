#pragma once
#include "ast.hpp"
#include "diagnostics.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace noe {
using ModuleId = std::uint32_t;
struct SourceFile { SourceId id=0; std::filesystem::path path; std::string text; };
struct ModuleUnit { ModuleId id=0; SourceId source=0; std::filesystem::path path; std::string declaredName; Program ast; std::vector<ModuleId> imports; };
class ModuleGraph {
public:
    bool load(const std::filesystem::path& root,Diagnostics& diagnostics);
    bool loadWithOverlay(const std::filesystem::path& root,const std::filesystem::path& overlayPath,std::string overlayText,Diagnostics& diagnostics);
    bool loadNdlBuffer(const std::string& logicalName,const std::string& archiveBytes,Diagnostics& diagnostics);
    void addNdlOverlayBuffer(const std::filesystem::path& logicalArchivePath,std::string archiveBytes);
    void clearNdlOverlays();
    const std::vector<ModuleUnit>& modules() const { return modules_; }
    const std::vector<SourceFile>& sources() const { return sources_; }
    Program mergedProgram() const;
private:
    bool loadOne(const std::filesystem::path& path,Diagnostics& diagnostics,ModuleId* out);
    std::vector<ModuleUnit> modules_;
    std::vector<SourceFile> sources_;
    std::unordered_map<std::string,ModuleId> byCanonicalPath_;
    std::unordered_map<std::string,std::string> overlays_;
    std::unordered_map<std::string,std::string> ndlBuffers_;
    std::unordered_map<std::string,std::filesystem::path> ndlLoadedArchives_;
    std::unordered_map<std::string,std::filesystem::path> ndlPackageEntries_;
};
} // namespace noe
