// SPDX-License-Identifier: GPL-3.0-only
// Execute the Noqeri-authored package command frontend using Stage-0 NIR.
// Native ABI services provide bounded platform I/O; no Node process is involved.
#include "noe.hpp"
#include "abi.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
namespace fs=std::filesystem;
namespace noqeri_registry { void operation(const std::string&,const std::vector<std::string>&); }
namespace {
struct Context {
    std::vector<std::string> arguments;
    std::string scratch;
    std::string error;
};
int32_t pkgArgument(void* ctx,const noqeri_abi_value*args,size_t n,
                    noqeri_abi_value*out,noqeri_abi_error*err) {
    auto&state=*static_cast<Context*>(ctx);
    if(n!=1||args[0].tag!=NOQERI_ABI_INT||args[0].as.integer<0) {
        err->message="sdkPkgArg expects a nonnegative index";return 1;
    }
    const size_t index=static_cast<size_t>(args[0].as.integer);
    state.scratch=index<state.arguments.size()?state.arguments[index]:"";
    out->tag=NOQERI_ABI_STRING;
    out->as.string={state.scratch.data(),state.scratch.size()};
    return 0;
}
int32_t pkgAction(void*ctx,const noqeri_abi_value*args,size_t n,
                  noqeri_abi_value*out,noqeri_abi_error*err){
    auto&state=*static_cast<Context*>(ctx);
    if(n!=1||args[0].tag!=NOQERI_ABI_STRING){err->message="sdkPkgRun expects action string";return 1;}
    const std::string action(args[0].as.string.data,args[0].as.string.size);
    try {
        std::vector<std::string>rest;
        if(state.arguments.size()>1)rest.assign(state.arguments.begin()+1,state.arguments.end());
        noqeri_registry::operation(action,rest);
        out->tag=NOQERI_ABI_NULL;return 0;
    } catch(const std::exception&e){
        state.error=e.what();err->message=state.error.c_str();return 1;
    }
}
int32_t pkgWrite(void*,const char*data,size_t len){if(data&&len)std::cout.write(data,static_cast<std::streamsize>(len));return std::cout.good()?0:1;}
}
int runNativeRegistryNoqeri(int argc,char**argv){
    std::vector<fs::path>paths;
    if(const char*root=std::getenv("NOQERI_STDLIB_ROOT"))
        paths.emplace_back(fs::path(root).parent_path()/"Tools/package/manager.noqeri");
    if(const char*root=std::getenv("NOQERI_SDK_ROOT"))
        paths.emplace_back(fs::path(root)/"share/noqeri/Tools/package/manager.noqeri");
    paths.emplace_back("Tools/package/manager.noqeri");
#ifdef NOQERI_PACKAGE_FRONTEND_PATH
    paths.emplace_back(NOQERI_PACKAGE_FRONTEND_PATH);
#endif
    paths.emplace_back("/usr/share/noqeri/Tools/package/manager.noqeri");
    paths.emplace_back("/usr/local/share/noqeri/Tools/package/manager.noqeri");
    fs::path source;
    for(const auto&candidate:paths)if(fs::is_regular_file(candidate)){source=candidate;break;}
    if(source.empty()){
        std::cerr<<"NQR-PKG7001: Noqeri package frontend not installed; reinstall the SDK\n";
        return 2;
    }
    Context ctx;
    for(int i=0;i<argc;++i)ctx.arguments.emplace_back(argv[i]);
    noqeri_abi_service services[]={
        {"sdkPkgArg",pkgArgument,&ctx},
        {"sdkPkgRun",pkgAction,&ctx}
    };
    const noqeri_abi abi={NOQERI_ABI_VERSION,sizeof(noqeri_abi),&ctx,
                          pkgWrite,nullptr,nullptr,2,services};
    noe::CompileOptions options;
    auto compiled=noe::compileFile(source,options);
    if(compiled.diagnostics.hasErrors()){
        compiled.diagnostics.print(source.string());
        return 2;
    }
    return noe::Interpreter(&abi).run(compiled.nir);
}