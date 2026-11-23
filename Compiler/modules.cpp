#include "noe.hpp"
#include "nodule_archive.hpp"
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {
Span remapSpan(Span span, SourceId sourceId) {
    span.source = sourceId;
    return span;
}
ExprPtr cloneAstExpr(const ExprPtr& expr, SourceId sourceId) {
    if (!expr) return nullptr;
    if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) {
        auto out = std::make_shared<LiteralExpr>(*lit);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    if (auto name = std::dynamic_pointer_cast<NameExpr>(expr)) {
        auto out = std::make_shared<NameExpr>(*name);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    if (auto unary = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
        auto out = std::make_shared<UnaryExpr>(*unary);
        out->span = remapSpan(out->span, sourceId);
        out->operand = cloneAstExpr(unary->operand, sourceId);
        return out;
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        auto out = std::make_shared<BinaryExpr>(*binary);
        out->span = remapSpan(out->span, sourceId);
        out->left = cloneAstExpr(binary->left, sourceId);
        out->right = cloneAstExpr(binary->right, sourceId);
        return out;
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
        auto out = std::make_shared<CallExpr>(*call);
        out->span = remapSpan(out->span, sourceId);
        out->callee = cloneAstExpr(call->callee, sourceId);
        out->args.clear();
        out->args.reserve(call->args.size());
        for (const auto& arg : call->args) out->args.push_back(cloneAstExpr(arg, sourceId));
        return out;
    }
    if (auto cast = std::dynamic_pointer_cast<CastExpr>(expr)) {
        auto out = std::make_shared<CastExpr>(*cast);
        out->span = remapSpan(out->span, sourceId);
        out->value = cloneAstExpr(cast->value, sourceId);
        return out;
    }
    if (auto index = std::dynamic_pointer_cast<IndexExpr>(expr)) {
        auto out = std::make_shared<IndexExpr>(*index);
        out->span = remapSpan(out->span, sourceId);
        out->object = cloneAstExpr(index->object, sourceId);
        out->index = cloneAstExpr(index->index, sourceId);
        return out;
    }
    if (auto member = std::dynamic_pointer_cast<MemberExpr>(expr)) {
        auto out = std::make_shared<MemberExpr>(*member);
        out->span = remapSpan(out->span, sourceId);
        out->object = cloneAstExpr(member->object, sourceId);
        return out;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        auto out = std::make_shared<ArrayExpr>(*array);
        out->span = remapSpan(out->span, sourceId);
        out->elements.clear();
        out->elements.reserve(array->elements.size());
        for (const auto& item : array->elements) out->elements.push_back(cloneAstExpr(item, sourceId));
        return out;
    }
    if (auto interp = std::dynamic_pointer_cast<InterpolatedStringExpr>(expr)) {
        auto out = std::make_shared<InterpolatedStringExpr>(*interp);
        out->span = remapSpan(out->span, sourceId);
        out->parts.clear();
        out->parts.reserve(interp->parts.size());
        for (const auto& part : interp->parts) out->parts.push_back(cloneAstExpr(part, sourceId));
        return out;
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        auto out = std::make_shared<LambdaExpr>(*lambda);
        out->span = remapSpan(out->span, sourceId);
        out->body = cloneAstExpr(lambda->body, sourceId);
        return out;
    }
    if (auto anon = std::dynamic_pointer_cast<AnonymousRecordExpr>(expr)) {
        auto out = std::make_shared<AnonymousRecordExpr>(*anon);
        out->span = remapSpan(out->span, sourceId);
        for (auto& field : out->fields) {
            field.span = remapSpan(field.span, sourceId);
            field.initializer = cloneAstExpr(field.initializer, sourceId);
        }
        return out;
    }
    return expr;
}
StmtPtr cloneAstStmt(const StmtPtr& stmt, SourceId sourceId);
std::shared_ptr<BlockStmt> cloneAstBlock(const std::shared_ptr<BlockStmt>& block, SourceId sourceId) {
    if (!block) return nullptr;
    auto out = std::make_shared<BlockStmt>(*block);
    out->span = remapSpan(out->span, sourceId);
    out->statements.clear();
    out->statements.reserve(block->statements.size());
    for (const auto& child : block->statements) out->statements.push_back(cloneAstStmt(child, sourceId));
    return out;
}
StmtPtr cloneAstStmt(const StmtPtr& stmt, SourceId sourceId) {
    if (!stmt) return nullptr;
    if (auto expr = std::dynamic_pointer_cast<ExprStmt>(stmt)) {
        auto out = std::make_shared<ExprStmt>(*expr);
        out->span = remapSpan(out->span, sourceId);
        out->expr = cloneAstExpr(expr->expr, sourceId);
        return out;
    }
    if (auto let = std::dynamic_pointer_cast<LetStmt>(stmt)) {
        auto out = std::make_shared<LetStmt>(*let);
        out->span = remapSpan(out->span, sourceId);
        out->initializer = cloneAstExpr(let->initializer, sourceId);
        return out;
    }
    if (auto block = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
        return cloneAstBlock(block, sourceId);
    }
    if (auto branch = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        auto out = std::make_shared<IfStmt>(*branch);
        out->span = remapSpan(out->span, sourceId);
        out->condition = cloneAstExpr(branch->condition, sourceId);
        out->thenBranch = cloneAstStmt(branch->thenBranch, sourceId);
        out->elseBranch = cloneAstStmt(branch->elseBranch, sourceId);
        return out;
    }
    if (auto loop = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        auto out = std::make_shared<WhileStmt>(*loop);
        out->span = remapSpan(out->span, sourceId);
        out->condition = cloneAstExpr(loop->condition, sourceId);
        out->body = cloneAstStmt(loop->body, sourceId);
        return out;
    }
    if (auto brk = std::dynamic_pointer_cast<BreakStmt>(stmt)) {
        auto out = std::make_shared<BreakStmt>(*brk);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    if (auto cont = std::dynamic_pointer_cast<ContinueStmt>(stmt)) {
        auto out = std::make_shared<ContinueStmt>(*cont);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    if (auto ret = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        auto out = std::make_shared<ReturnStmt>(*ret);
        out->span = remapSpan(out->span, sourceId);
        out->value = cloneAstExpr(ret->value, sourceId);
        return out;
    }
    if (auto thr = std::dynamic_pointer_cast<ThrowStmt>(stmt)) {
        auto out = std::make_shared<ThrowStmt>(*thr);
        out->span = remapSpan(out->span, sourceId);
        out->value = cloneAstExpr(thr->value, sourceId);
        return out;
    }
    if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
        auto out = std::make_shared<FunctionStmt>(*fn);
        out->span = remapSpan(out->span, sourceId);
        for (auto& param : out->params) param.span = remapSpan(param.span, sourceId);
        out->body = cloneAstBlock(fn->body, sourceId);
        return out;
    }
    if (auto rec = std::dynamic_pointer_cast<RecordStmt>(stmt)) {
        auto out = std::make_shared<RecordStmt>(*rec);
        out->span = remapSpan(out->span, sourceId);
        for (auto& field : out->fields) field.span = remapSpan(field.span, sourceId);
        return out;
    }
    if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) {
        auto out = std::make_shared<ImportStmt>(*imp);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    if (auto mod = std::dynamic_pointer_cast<ModuleStmt>(stmt)) {
        auto out = std::make_shared<ModuleStmt>(*mod);
        out->span = remapSpan(out->span, sourceId);
        return out;
    }
    return stmt;
}
Program cloneAstProgram(const Program& program, SourceId sourceId) {
    Program out;
    out.statements.reserve(program.statements.size());
    for (const auto& stmt : program.statements) out.statements.push_back(cloneAstStmt(stmt, sourceId));
    return out;
}
struct CachedModuleEntry {
    std::filesystem::file_time_type mtime{};
    std::uintmax_t fileSize = 0;
    std::string text;
    Program ast;
};
std::mutex& moduleCacheMutex() {
    static std::mutex mu;
    return mu;
}
std::unordered_map<std::string, CachedModuleEntry>& moduleCache() {
    static std::unordered_map<std::string, CachedModuleEntry> cache;
    return cache;
}
} // namespace

void ModuleGraph::resetGraphState(const std::filesystem::path& rootHint){
    modules_.clear();
    sources_.clear();
    byCanonicalPath_.clear();
    overlays_.clear();
    ndlLoadedArchives_.clear();
    ndlPackageEntries_.clear();
    lockPins_.clear();
    loadedNdlVersions_.clear();
    loadedNdlDigests_.clear();
    loadingNdlArchives_.clear();
    loadingModules_.clear();
    ndlSearchRoots_.clear();

    std::error_code ec;
    auto startDir=rootHint.has_parent_path()?rootHint.parent_path():rootHint;
    if(startDir.empty())startDir=".";
    auto canon=std::filesystem::weakly_canonical(startDir,ec);
    if(ec)canon=std::filesystem::absolute(startDir,ec);
    canon=canon.lexically_normal();

    std::filesystem::path lockFilePath;
    for(auto dir=canon;!dir.empty();){
        ndlSearchRoots_.push_back(dir);
        ndlSearchRoots_.push_back(dir/"nodules");
        ndlSearchRoots_.push_back(dir/".noqeri"/"cache");
        ndlSearchRoots_.push_back(dir/".noqeri"/"cache"/"registry-v1");
        ndlSearchRoots_.push_back(dir/"distribution");
        if(lockFilePath.empty()){
            const auto cand=dir/"noqeri.lock";
            if(std::filesystem::is_regular_file(cand,ec))lockFilePath=cand;
        }
        const auto up=dir.parent_path();
        if(up==dir)break;
        dir=up;
    }
    if(const char* envCache=std::getenv("NOQERI_NODULE_CACHE");envCache&&*envCache)
        ndlSearchRoots_.emplace_back(envCache);
    if(const char* home=std::getenv("NOQERI_HOME");home&&*home)
        ndlSearchRoots_.push_back(std::filesystem::path(home)/"cache"/"registry-v1");

    if(!lockFilePath.empty()){
        std::ifstream lf(lockFilePath,std::ios::binary);
        if(lf){
            std::string line;
            while(std::getline(lf,line)){
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line.empty()||line[0]=='#')continue;
                std::istringstream iss(line);
                std::string tok;
                iss>>tok;
                if(tok=="package"){
                    std::string alias,coord,ver,integrity,entry;
                    if(iss>>alias>>coord>>ver>>integrity){
                        std::string hex=integrity;
                        if(hex.rfind("sha256:",0)==0)hex=hex.substr(7);
                        NdlLockPin pin{ver,hex};
                        lockPins_[coord]=pin;
                        const auto slash=coord.find('/');
                        if(slash!=std::string::npos){
                            const std::string shortName=coord.substr(slash+1);
                            if(!lockPins_.count(shortName))lockPins_[shortName]=pin;
                        }
                    }
                }
            }
        }
    }
}

std::filesystem::path ModuleGraph::resolveNdlArchiveCandidate(const std::string& spec,const std::string& requiredVersion,const std::filesystem::path& fromDir) const{
    if(spec.empty()||spec[0]=='/'||spec.find("..")!=std::string::npos||spec.find('\\')!=std::string::npos)
        return {};
    std::string effectiveVer=requiredVersion;
    if(effectiveVer.empty()){
        if(auto it=lockPins_.find(spec);it!=lockPins_.end())effectiveVer=it->second.version;
    }
    std::string dashed=spec;
    std::replace(dashed.begin(),dashed.end(),'/','-');
    const auto slashPos=spec.find('/');
    const std::string shortName=(slashPos!=std::string::npos)?spec.substr(slashPos+1):spec;

    std::vector<std::string> relCandidates;
    if(!effectiveVer.empty()){
        relCandidates.push_back(dashed+"-"+effectiveVer+".ndl");
        relCandidates.push_back(shortName+"-"+effectiveVer+".ndl");
        relCandidates.push_back(spec+"-"+effectiveVer+".ndl");
        if(slashPos!=std::string::npos){
            const std::string ns=spec.substr(0,slashPos);
            relCandidates.push_back(ns+"/"+shortName+"/"+effectiveVer+"/"+shortName+".ndl");
            relCandidates.push_back(ns+"/"+shortName+"/"+effectiveVer+"/archive.ndl");
        }
    }
    const std::string directNdl=(std::filesystem::path(spec).extension()==".ndl")?spec:(spec+".ndl");
    relCandidates.push_back(directNdl);
    if(dashed!=spec)relCandidates.push_back(dashed+".ndl");
    if(shortName!=spec)relCandidates.push_back(shortName+".ndl");

    auto canonicalStr=[](const std::filesystem::path& p){
        std::error_code kek;
        auto w=std::filesystem::weakly_canonical(p,kek);
        if(kek)w=std::filesystem::absolute(p,kek);
        return w.lexically_normal().generic_string();
    };

    // 1. Check in-memory ndlBuffers_ deterministically
    std::vector<std::string> bufKeys;
    bufKeys.reserve(ndlBuffers_.size());
    for(const auto& kv:ndlBuffers_)bufKeys.push_back(kv.first);
    std::sort(bufKeys.begin(),bufKeys.end());
    for(const auto& rel:relCandidates){
        const std::string directKey=canonicalStr(fromDir/rel);
        if(ndlBuffers_.count(directKey))return fromDir/rel;
        const std::string suffix="/"+rel;
        for(const auto& k:bufKeys){
            if(k==rel||(k.size()>=suffix.size()&&k.compare(k.size()-suffix.size(),suffix.size(),suffix)==0))
                return std::filesystem::path(k);
        }
    }

    // 2. Check fromDir local paths, then ancestor search roots
    std::vector<std::filesystem::path> dirs;
    if(!fromDir.empty()){
        dirs.push_back(fromDir);
        dirs.push_back(fromDir/"nodules");
        dirs.push_back(fromDir/".noqeri"/"cache");
    }
    dirs.insert(dirs.end(),ndlSearchRoots_.begin(),ndlSearchRoots_.end());

    std::error_code ec;
    for(const auto& dir:dirs){
        for(const auto& rel:relCandidates){
            const auto cand=dir/rel;
            const auto st=std::filesystem::symlink_status(cand,ec);
            if(!ec&&std::filesystem::exists(st)&&!std::filesystem::is_symlink(st)&&std::filesystem::is_regular_file(st))
                return cand;
        }
    }
    return {};
}

bool ModuleGraph::load(const std::filesystem::path&root,Diagnostics&diagnostics){resetGraphState(root);ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
bool ModuleGraph::loadWithOverlay(const std::filesystem::path&root,const std::filesystem::path&overlayPath,std::string overlayText,Diagnostics&diagnostics){resetGraphState(root);std::error_code ec;auto canonical=std::filesystem::weakly_canonical(overlayPath,ec);if(ec)canonical=std::filesystem::absolute(overlayPath,ec);overlays_[canonical.lexically_normal().generic_string()]=std::move(overlayText);ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
void ModuleGraph::addNdlOverlayBuffer(const std::filesystem::path& logicalArchivePath,std::string archiveBytes){
    std::error_code ec;
    auto canonical=std::filesystem::weakly_canonical(logicalArchivePath,ec);
    if(ec)canonical=std::filesystem::absolute(logicalArchivePath,ec);
    ndlBuffers_[canonical.lexically_normal().generic_string()]=std::move(archiveBytes);
}
void ModuleGraph::clearNdlOverlays(){ndlBuffers_.clear();}
bool ModuleGraph::loadNdlBuffer(const std::string& logicalName,const std::string& archiveBytes,Diagnostics& diagnostics){
    std::filesystem::path logical=logicalName;
    if(logical.extension()!=".ndl")logical+=".ndl";
    resetGraphState(logical);
    std::error_code ec;
    auto canonical=std::filesystem::weakly_canonical(logical,ec);
    if(ec)canonical=std::filesystem::absolute(logical,ec);
    canonical=canonical.lexically_normal();
    const std::string prevBuf=[&](){
        auto it=ndlBuffers_.find(canonical.generic_string());
        return it!=ndlBuffers_.end()?it->second:std::string{};
    }();
    const bool hadPrev=ndlBuffers_.count(canonical.generic_string())>0;
    ndlBuffers_[canonical.generic_string()]=archiveBytes;
    ModuleId rootId=0;
    const bool ok=loadOne(canonical,diagnostics,&rootId)&&!diagnostics.hasErrors();
    if(hadPrev)ndlBuffers_[canonical.generic_string()]=prevBuf;
    else ndlBuffers_.erase(canonical.generic_string());
    return ok;
}
bool ModuleGraph::loadOne(const std::filesystem::path&input,Diagnostics&diagnostics,ModuleId*out){std::error_code ec;auto canonical=std::filesystem::weakly_canonical(input,ec);if(ec){canonical=std::filesystem::absolute(input,ec);if(ec){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}}canonical=canonical.lexically_normal();const auto key=canonical.generic_string();if(auto found=byCanonicalPath_.find(key);found!=byCanonicalPath_.end()){if(out)*out=found->second;return true;}
if(canonical.extension()==".ndl"||ndlBuffers_.count(key)){
    if(loadingNdlArchives_.count(key)){
        diagnostics.error("NQR-M6012",{},"circular .ndl dependency detected involving '"+input.string()+"'");
        return false;
    }
    auto loadedIt=ndlLoadedArchives_.find(key);
    std::filesystem::path entryVirtualPath;
    if(loadedIt!=ndlLoadedArchives_.end()){
        entryVirtualPath=loadedIt->second;
    }else{
        loadingNdlArchives_.insert(key);
        noqeri_nodule::NdlArchive archive;
        std::string err;
        bool ok=false;
        if(auto bufIt=ndlBuffers_.find(key);bufIt!=ndlBuffers_.end()){
            ok=noqeri_nodule::loadNdlArchiveFromBytes(bufIt->second,archive,err);
        }else{
            ok=noqeri_nodule::loadNdlArchiveFromFile(canonical,archive,err);
        }
        if(!ok){
            loadingNdlArchives_.erase(key);
            diagnostics.error("NQR-M6010",{},"invalid .ndl archive '"+input.string()+"': "+err,"ensure the archive is a verified NDL\\x01 or gzip+ustar container matching ABI v2 and Noqeri 0.1.5");
            return false;
        }
        if(archive.manifest.abiVersion!=2||(archive.manifest.languageVersion!="0.1.5"&&archive.manifest.languageVersion!="0.1.5.1")){
            loadingNdlArchives_.erase(key);
            diagnostics.error("NQR-M6011",{},"incompatible .ndl archive ABI/language version in '"+input.string()+"'");
            return false;
        }
        if(auto lockIt=lockPins_.find(archive.manifest.name);lockIt!=lockPins_.end()){
            if((!lockIt->second.version.empty()&&lockIt->second.version!=archive.manifest.version)||
               (!lockIt->second.sha256Hex.empty()&&lockIt->second.sha256Hex!=archive.archiveSha256Hex)){
                loadingNdlArchives_.erase(key);
                diagnostics.error("NQR-M6014",{},"lockfile pin mismatch for .ndl package '"+archive.manifest.name+"' (expected "+
                    lockIt->second.version+"#"+lockIt->second.sha256Hex+", got "+archive.manifest.version+"#"+archive.archiveSha256Hex+")");
                return false;
            }
        }
        if(auto verIt=loadedNdlVersions_.find(archive.manifest.name);verIt!=loadedNdlVersions_.end()){
            if(verIt->second!=archive.manifest.version){
                loadingNdlArchives_.erase(key);
                diagnostics.error("NQR-M6013",{},"version conflict for .ndl package '"+archive.manifest.name+"': loaded "+verIt->second+" vs "+archive.manifest.version);
                return false;
            }
            if(auto digIt=loadedNdlDigests_.find(archive.manifest.name);digIt!=loadedNdlDigests_.end()&&digIt->second!=archive.archiveSha256Hex){
                loadingNdlArchives_.erase(key);
                diagnostics.error("NQR-M6014",{},"SHA-256 digest mismatch for duplicate .ndl package '"+archive.manifest.name+"'");
                return false;
            }
        }else{
            loadedNdlVersions_[archive.manifest.name]=archive.manifest.version;
            loadedNdlDigests_[archive.manifest.name]=archive.archiveSha256Hex;
        }
        for(const auto& entry:archive.entries){
            const auto vpath=(canonical/entry.path).lexically_normal();
            overlays_[vpath.generic_string()]=entry.data;
        }
        entryVirtualPath=(canonical/archive.manifest.entry).lexically_normal();
        ndlLoadedArchives_[key]=entryVirtualPath;
        ndlPackageEntries_[archive.manifest.name]=entryVirtualPath;
        if(const auto slash=archive.manifest.name.find('/');slash!=std::string::npos){
            const std::string shortName=archive.manifest.name.substr(slash+1);
            if(!ndlPackageEntries_.count(shortName))ndlPackageEntries_[shortName]=entryVirtualPath;
        }
        for(const auto& exp:archive.manifest.exports){
            ndlPackageEntries_[archive.manifest.name+"/"+exp]=(canonical/exp).lexically_normal();
        }

        // Resolve transitive .ndl dependencies deterministically (sorted by name)
        std::vector<ModuleId> ndlTransitiveDeps;
        auto deps=archive.manifest.dependencies;
        std::sort(deps.begin(),deps.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        for(const auto& dep:deps){
            if(auto loadedVer=loadedNdlVersions_.find(dep.name);loadedVer!=loadedNdlVersions_.end()){
                if(loadedVer->second!=dep.version){
                    loadingNdlArchives_.erase(key);
                    diagnostics.error("NQR-M6013",{},"transitive version conflict for .ndl package '"+dep.name+"': "+loadedVer->second+" vs "+dep.version);
                    return false;
                }
                if(!dep.sha256Hex.empty()){
                    if(auto digIt=loadedNdlDigests_.find(dep.name);digIt!=loadedNdlDigests_.end()&&digIt->second!=dep.sha256Hex){
                        loadingNdlArchives_.erase(key);
                        diagnostics.error("NQR-M6014",{},"transitive SHA-256 mismatch for .ndl package '"+dep.name+"'");
                        return false;
                    }
                }
            }
            const auto depPath=resolveNdlArchiveCandidate(dep.name,dep.version,canonical.parent_path());
            if(depPath.empty()){
                loadingNdlArchives_.erase(key);
                diagnostics.error("NQR-M6002",{},"cannot find transitive .ndl dependency '"+dep.name+"@"+dep.version+"' required by '"+archive.manifest.name+"'");
                return false;
            }
            ModuleId depEntryId=0;
            if(!loadOne(depPath,diagnostics,&depEntryId)){
                loadingNdlArchives_.erase(key);
                return false;
            }
            ndlTransitiveDeps.push_back(depEntryId);
            if(!dep.sha256Hex.empty()){
                if(auto digIt=loadedNdlDigests_.find(dep.name);digIt!=loadedNdlDigests_.end()&&digIt->second!=dep.sha256Hex){
                    loadingNdlArchives_.erase(key);
                    diagnostics.error("NQR-M6014",{},"transitive SHA-256 digest pin mismatch for .ndl dependency '"+dep.name+"'");
                    return false;
                }
            }
        }
        loadingNdlArchives_.erase(key);
        ModuleId entryId=0;
        if(!loadOne(entryVirtualPath,diagnostics,&entryId))return false;
        if(entryId<modules_.size()){
            for(const ModuleId depId:ndlTransitiveDeps){
                if(std::find(modules_[entryId].imports.begin(),modules_[entryId].imports.end(),depId)==modules_[entryId].imports.end()){
                    modules_[entryId].imports.push_back(depId);
                }
            }
        }
        byCanonicalPath_[key]=entryId;
        if(out)*out=entryId;
        return true;
    }
    ModuleId entryId=0;
    if(!loadOne(entryVirtualPath,diagnostics,&entryId))return false;
    byCanonicalPath_[key]=entryId;
    if(out)*out=entryId;
    return true;
}
if(loadingModules_.count(key)){
    diagnostics.error("NQR-M6012",{},"circular module import detected involving '"+input.string()+"'");
    return false;
}
const auto overlay=overlays_.find(key);if(overlay==overlays_.end()&&!std::filesystem::exists(canonical)){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}loadingModules_.insert(key);const ModuleId id=static_cast<ModuleId>(modules_.size());const SourceId sourceId=static_cast<SourceId>(sources_.size()+1);byCanonicalPath_[key]=id;std::string text;Program ast;bool cacheHit=false;std::filesystem::file_time_type diskMtime{};std::uintmax_t diskSize=0;bool hasDiskStat=false;if(overlay!=overlays_.end()){text=overlay->second;}else{std::error_code statEc;diskMtime=std::filesystem::last_write_time(canonical,statEc);if(!statEc)diskSize=std::filesystem::file_size(canonical,statEc);hasDiskStat=!statEc;if(hasDiskStat){std::lock_guard<std::mutex> lock(moduleCacheMutex());auto it=moduleCache().find(key);if(it!=moduleCache().end()&&it->second.mtime==diskMtime&&it->second.fileSize==diskSize){text=it->second.text;ast=cloneAstProgram(it->second.ast,sourceId);cacheHit=true;}}if(!cacheHit){try{text=readTextFile(canonical);}catch(const std::exception&error){loadingModules_.erase(key);diagnostics.error("NQR-M6001",{},error.what());return false;}}}diagnostics.registerSource(sourceId,canonical.string(),text);sources_.push_back(SourceFile{sourceId,canonical,text});if(!cacheHit){auto syntax=preprocessGenericSyntax(text);Lexer lexer(syntax.source,diagnostics,sourceId,false);auto tokens=lexer.lex();if(diagnostics.hasErrors()){loadingModules_.erase(key);return false;}Parser parser(std::move(tokens),diagnostics);ast=parser.parse();applyGenericSyntax(ast,syntax);if(diagnostics.hasErrors()){loadingModules_.erase(key);return false;}if(overlay==overlays_.end()&&hasDiskStat){std::lock_guard<std::mutex> lock(moduleCacheMutex());if(moduleCache().size()>512)moduleCache().clear();moduleCache()[key]=CachedModuleEntry{diskMtime,diskSize,text,cloneAstProgram(ast,0)};}}ModuleUnit unit;unit.id=id;unit.source=sourceId;unit.path=canonical;unit.ast=ast;for(const auto&statement:ast.statements)if(auto module=std::dynamic_pointer_cast<ModuleStmt>(statement)){unit.declaredName=module->name;break;}modules_.push_back(std::move(unit));for(const auto&statement:ast.statements){auto imported=std::dynamic_pointer_cast<ImportStmt>(statement);if(!imported)continue;std::filesystem::path child=imported->path;
        if(child.extension().empty())child+=".noqeri";
        const bool standard=imported->path.rfind("std/",0)==0 ||
                            imported->path.rfind("ai/",0)==0;
        if(standard) {
            const auto relative=child.lexically_normal();
            if(relative.is_absolute()||relative.generic_string().find("..")!=std::string::npos) {
                loadingModules_.erase(key);
                diagnostics.error("NQR-M6002",imported->span,
                    "unsafe standard import '"+imported->path+"'",
                    "use a module path such as import \"std/list\"");
                return false;
            }
            std::vector<std::filesystem::path> roots;
            if(const char* root=std::getenv("NOQERI_STDLIB_ROOT");root&&*root)
                roots.emplace_back(root);
            for(auto dir=canonical.parent_path();!dir.empty();) {
                roots.push_back(dir/"Lib");
                const auto up=dir.parent_path();
                if(up==dir)break;
                dir=up;
            }
            bool resolved=false;
            for(const auto& root:roots) {
                const auto candidate=root/relative;
                if(std::filesystem::is_regular_file(candidate)) {
                    child=candidate;resolved=true;break;
                }
            }
            if(!resolved) {
                loadingModules_.erase(key);
                diagnostics.error("NQR-M6002",imported->span,
                    "standard module '"+imported->path+"' was not found",
                    "install the Noqeri Lib tree or set NOQERI_STDLIB_ROOT to its absolute directory");
                return false;
            }
        } else if(child.is_relative()) child=canonical.parent_path()/child;
        const auto childKey=[&](const std::filesystem::path& p){
            std::error_code kek;
            auto w=std::filesystem::weakly_canonical(p,kek);
            if(kek)w=std::filesystem::absolute(p,kek);
            return w.lexically_normal().generic_string();
        };
        const bool existsNormal=std::filesystem::exists(child)||
            overlays_.count(childKey(child))>0||
            ndlBuffers_.count(childKey(child))>0;
        if(!existsNormal && !standard) {
            if(auto pkgIt=ndlPackageEntries_.find(imported->path);pkgIt!=ndlPackageEntries_.end()){
                child=pkgIt->second;
            }else{
                const auto cand=resolveNdlArchiveCandidate(imported->path,"",canonical.parent_path());
                if(!cand.empty())child=cand;
            }
        }
        const auto resolvedKey=childKey(child);
        if(!std::filesystem::exists(child) &&
           overlays_.find(resolvedKey)==overlays_.end() &&
           ndlBuffers_.find(resolvedKey)==ndlBuffers_.end()) {
            loadingModules_.erase(key);
            diagnostics.error("NQR-M6002",imported->span,
                "cannot find imported module '"+imported->path+"'",
                "check the spelling; local files use import \"./helpers.noqeri\" and standard modules use import \"std/list\"");
            return false;
        }
        ModuleId childId=0;if(!loadOne(child,diagnostics,&childId)){loadingModules_.erase(key);return false;}modules_[id].imports.push_back(childId);}loadingModules_.erase(key);if(out)*out=id;return true;}
Program ModuleGraph::mergedProgram()const{Program merged;if(modules_.empty())return merged;std::unordered_set<ModuleId>visited;std::function<void(ModuleId)>append=[&](ModuleId id){if(visited.count(id)||id>=modules_.size())return;visited.insert(id);for(const auto dep:modules_[id].imports)append(dep);for(const auto&statement:modules_[id].ast.statements){if(std::dynamic_pointer_cast<ImportStmt>(statement))continue;merged.statements.push_back(statement);}};append(0);return merged;}
} // namespace noe
