#include "noe.hpp"
namespace noe {
CompileResult WorkspaceCompiler::compileOverlay(const std::filesystem::path&rootFile,std::string source,const CompileOptions&options)const{
    Diagnostics d;
    auto graph=std::make_shared<ModuleGraph>();
    if(!graph->loadWithOverlay(rootFile,rootFile,std::move(source),d))return compileProgram({},std::move(d),options,std::move(graph));
    Program merged=graph->mergedProgram();
    return compileProgram(std::move(merged),std::move(d),options,std::move(graph));
}
} // namespace noe
