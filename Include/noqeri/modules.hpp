#pragma once
#include "ast.hpp"
#include "diagnostics.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noe {
using ModuleId = std::uint32_t;
struct SourceFile { SourceId id=0; std::filesystem::path path; std::string text; };
struct ModuleUnit { ModuleId id=0; SourceId source=0; std::filesystem::path path; std::string declaredName; Program ast; std::vector<ModuleId> imports; };
struct NdlLockPin { std::string version; std::string sha256Hex; };
class ModuleGraph {
public:
    bool load(const std::filesystem::path& root,Diagnostics& diagnostics);
    bool loadWithOverlay(const std::filesystem::path& root,const std::filesystem::path& overlayPath,std::string overlayText,Diagnostics& diagnostics);
    bool loadNdlBuffer(const std::string& logicalName,const std::string& archiveBytes,Diagnostics& diagnostics);
    void addNdlOverlayBuffer(const std::filesystem::path& logicalArchivePath,std::string archiveBytes);
    void clearNdlOverlays();
    const std::vector<ModuleUnit>& modules() const { return modules_; }
    const std::vector<SourceFile>& sources() const { return sources_; }
    const std::unordered_map<std::string,std::string>& loadedNdlDigests() const { return loadedNdlDigests_; }
    Program mergedProgram() const;
private:
    void resetGraphState(const std::filesystem::path& rootHint);
    std::filesystem::path resolveNdlArchiveCandidate(const std::string& spec,const std::string& requiredVersion,const std::filesystem::path& fromDir) const;
    bool loadOne(const std::filesystem::path& path,Diagnostics& diagnostics,ModuleId* out);
    std::vector<ModuleUnit> modules_;
    std::vector<SourceFile> sources_;
    std::unordered_map<std::string,ModuleId> byCanonicalPath_;
    std::unordered_map<std::string,std::string> overlays_;
    std::unordered_map<std::string,std::string> ndlBuffers_;
    std::unordered_map<std::string,std::filesystem::path> ndlLoadedArchives_;
    std::unordered_map<std::string,std::filesystem::path> ndlPackageEntries_;
    std::unordered_map<std::string,NdlLockPin> lockPins_;
    std::unordered_map<std::string,std::string> loadedNdlVersions_;
    std::unordered_map<std::string,std::string> loadedNdlDigests_;
    std::unordered_set<std::string> loadingNdlArchives_;
    std::unordered_set<std::string> loadingModules_;
    std::vector<std::filesystem::path> ndlSearchRoots_;
};
} // namespace noe
