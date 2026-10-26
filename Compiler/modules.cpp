#include "noe.hpp"
#include <filesystem>
#include <cstdlib>
#include <functional>
#include <system_error>
#include <unordered_set>

namespace noe {
bool ModuleGraph::load(const std::filesystem::path&root,Diagnostics&diagnostics){modules_.clear();sources_.clear();byCanonicalPath_.clear();overlays_.clear();ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
bool ModuleGraph::loadWithOverlay(const std::filesystem::path&root,const std::filesystem::path&overlayPath,std::string overlayText,Diagnostics&diagnostics){modules_.clear();sources_.clear();byCanonicalPath_.clear();overlays_.clear();std::error_code ec;auto canonical=std::filesystem::weakly_canonical(overlayPath,ec);if(ec)canonical=std::filesystem::absolute(overlayPath,ec);overlays_[canonical.generic_string()]=std::move(overlayText);ModuleId rootId=0;return loadOne(root,diagnostics,&rootId)&&!diagnostics.hasErrors();}
bool ModuleGraph::loadOne(const std::filesystem::path&input,Diagnostics&diagnostics,ModuleId*out){std::error_code ec;auto canonical=std::filesystem::weakly_canonical(input,ec);if(ec){canonical=std::filesystem::absolute(input,ec);if(ec){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}}const auto key=canonical.generic_string();if(auto found=byCanonicalPath_.find(key);found!=byCanonicalPath_.end()){if(out)*out=found->second;return true;}const auto overlay=overlays_.find(key);if(overlay==overlays_.end()&&!std::filesystem::exists(canonical)){diagnostics.error("NQR-M6000",{},"module source not found: "+input.string(),"check import paths relative to the importing file");return false;}const ModuleId id=static_cast<ModuleId>(modules_.size());const SourceId sourceId=static_cast<SourceId>(sources_.size()+1);byCanonicalPath_[key]=id;std::string text;if(overlay!=overlays_.end())text=overlay->second;else try{text=readTextFile(canonical);}catch(const std::exception&error){diagnostics.error("NQR-M6001",{},error.what());return false;}diagnostics.registerSource(sourceId,canonical.string(),text);sources_.push_back(SourceFile{sourceId,canonical,text});auto syntax=preprocessGenericSyntax(text);Lexer lexer(syntax.source,diagnostics,sourceId,false);auto tokens=lexer.lex();if(diagnostics.hasErrors())return false;Parser parser(std::move(tokens),diagnostics);Program ast=parser.parse();applyGenericSyntax(ast,syntax);if(diagnostics.hasErrors())return false;ModuleUnit unit;unit.id=id;unit.source=sourceId;unit.path=canonical;unit.ast=ast;for(const auto&statement:ast.statements)if(auto module=std::dynamic_pointer_cast<ModuleStmt>(statement)){unit.declaredName=module->name;break;}modules_.push_back(std::move(unit));for(const auto&statement:ast.statements){auto imported=std::dynamic_pointer_cast<ImportStmt>(statement);if(!imported)continue;std::filesystem::path child=imported->path;
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
