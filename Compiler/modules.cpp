#include "noe.hpp"
#include <filesystem>
#include <cstdlib>
#include <functional>
#include <mutex>
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

bool ModuleGraph::load(const std::filesystem::path&root,Diagnostics&diagnostics){modules_.clear();sources_.clear();byCanonicalPath_.clear();overlays_.clear();ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
bool ModuleGraph::loadWithOverlay(const std::filesystem::path&root,const std::filesystem::path&overlayPath,std::string overlayText,Diagnostics&diagnostics){modules_.clear();sources_.clear();byCanonicalPath_.clear();overlays_.clear();std::error_code ec;auto canonical=std::filesystem::weakly_canonical(overlayPath,ec);if(ec)canonical=std::filesystem::absolute(overlayPath,ec);overlays_[canonical.generic_string()]=std::move(overlayText);ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
bool ModuleGraph::loadOne(const std::filesystem::path&input,Diagnostics&diagnostics,ModuleId*out){std::error_code ec;auto canonical=std::filesystem::weakly_canonical(input,ec);if(ec){canonical=std::filesystem::absolute(input,ec);if(ec){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}}const auto key=canonical.generic_string();if(auto found=byCanonicalPath_.find(key);found!=byCanonicalPath_.end()){if(out)*out=found->second;return true;}const auto overlay=overlays_.find(key);if(overlay==overlays_.end()&&!std::filesystem::exists(canonical)){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}const ModuleId id=static_cast<ModuleId>(modules_.size());const SourceId sourceId=static_cast<SourceId>(sources_.size()+1);byCanonicalPath_[key]=id;std::string text;Program ast;bool cacheHit=false;std::filesystem::file_time_type diskMtime{};std::uintmax_t diskSize=0;bool hasDiskStat=false;if(overlay!=overlays_.end()){text=overlay->second;}else{std::error_code statEc;diskMtime=std::filesystem::last_write_time(canonical,statEc);if(!statEc)diskSize=std::filesystem::file_size(canonical,statEc);hasDiskStat=!statEc;if(hasDiskStat){std::lock_guard<std::mutex> lock(moduleCacheMutex());auto it=moduleCache().find(key);if(it!=moduleCache().end()&&it->second.mtime==diskMtime&&it->second.fileSize==diskSize){text=it->second.text;ast=cloneAstProgram(it->second.ast,sourceId);cacheHit=true;}}if(!cacheHit){try{text=readTextFile(canonical);}catch(const std::exception&error){diagnostics.error("NQR-M6001",{},error.what());return false;}}}diagnostics.registerSource(sourceId,canonical.string(),text);sources_.push_back(SourceFile{sourceId,canonical,text});if(!cacheHit){auto syntax=preprocessGenericSyntax(text);Lexer lexer(syntax.source,diagnostics,sourceId,false);auto tokens=lexer.lex();if(diagnostics.hasErrors())return false;Parser parser(std::move(tokens),diagnostics);ast=parser.parse();applyGenericSyntax(ast,syntax);if(diagnostics.hasErrors())return false;if(overlay==overlays_.end()&&hasDiskStat){std::lock_guard<std::mutex> lock(moduleCacheMutex());if(moduleCache().size()>512)moduleCache().clear();moduleCache()[key]=CachedModuleEntry{diskMtime,diskSize,text,cloneAstProgram(ast,0)};}}ModuleUnit unit;unit.id=id;unit.source=sourceId;unit.path=canonical;unit.ast=ast;for(const auto&statement:ast.statements)if(auto module=std::dynamic_pointer_cast<ModuleStmt>(statement)){unit.declaredName=module->name;break;}modules_.push_back(std::move(unit));for(const auto&statement:ast.statements){auto imported=std::dynamic_pointer_cast<ImportStmt>(statement);if(!imported)continue;std::filesystem::path child=imported->path;
        if(child.extension().empty())child+=".noqeri";
        const bool standard=imported->path.rfind("std/",0)==0 ||
                            imported->path.rfind("ai/",0)==0;
        if(standard) {
            // Standard imports are stable module names, not paths escaping
            // the library. Resolve from an explicit installation root, then
            // from an enclosing checkout's Lib directory.
            const auto relative=child.lexically_normal();
            if(relative.is_absolute()||relative.generic_string().find("..")!=std::string::npos) {
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
                diagnostics.error("NQR-M6002",imported->span,
                    "standard module '"+imported->path+"' was not found",
                    "install the Noqeri Lib tree or set NOQERI_STDLIB_ROOT to its absolute directory");
                return false;
            }
        } else if(child.is_relative()) child=canonical.parent_path()/child;
        if(!std::filesystem::is_regular_file(child) &&
           overlays_.find(std::filesystem::weakly_canonical(child).generic_string())==overlays_.end()) {
            diagnostics.error("NQR-M6002",imported->span,
                "cannot find imported module '"+imported->path+"'",
                "check the spelling; local files use import \"./helpers.noqeri\" and standard modules use import \"std/list\"");
            return false;
        }
        ModuleId childId=0;if(!loadOne(child,diagnostics,&childId))return false;modules_[id].imports.push_back(childId);}if(out)*out=id;return true;}
Program ModuleGraph::mergedProgram()const{Program merged;if(modules_.empty())return merged;std::unordered_set<ModuleId>visited;std::function<void(ModuleId)>append=[&](ModuleId id){if(visited.count(id)||id>=modules_.size())return;visited.insert(id);for(const auto dep:modules_[id].imports)append(dep);for(const auto&statement:modules_[id].ast.statements){if(std::dynamic_pointer_cast<ImportStmt>(statement))continue;merged.statements.push_back(statement);}};append(0);return merged;}
} // namespace noe
