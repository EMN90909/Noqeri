#include "noe.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace noe {

int runProductionDoctor(const std::filesystem::path&root){
    struct Check{std::filesystem::path path;std::string label;};
    const std::vector<Check>required={
        {"README.md","public README"},{"LICENSE","GPL-3.0-only license"},{"LEGAL.md","legal notice"},{"SECURITY.md","security policy"},{"CONTRIBUTING.md","contribution guide"},{"CHANGELOG.md","change log"},
        {"project.nqr","noqeri project manifest"},{"noqeri.lock","noqeri lock file"},{"bootstrap.ps1","Windows first-run bootstrap"},{"bootstrap.sh","POSIX first-run bootstrap"},{"Brand/noqeri-logo.webp","canonical noqeri logo"},{"Brand/noqeri-mark.png","IDE/file mark"},{"Brand/noqeri-banner.txt","terminal NOQERI wordmark"},
        {"Include/noqeri/noqeri.hpp","public umbrella header"},{"Include/noqeri/version.hpp","version API"},{"Include/noqeri/diagnostics.hpp","diagnostics API"},{"Include/noqeri/types.hpp","types API"},{"Include/noqeri/ast.hpp","AST API"},{"Include/noqeri/nir.hpp","NIR API"},{"Include/noqeri/modules.hpp","modules API"},{"Include/noqeri/packages.hpp","packages API"},{"Include/noqeri/targets.hpp","targets API"},{"Include/noqeri/runtime.hpp","runtime API"},{"Include/noqeri/compiler.hpp","compiler API"},{"Include/noqeri/tools.hpp","tooling API"},{"Include/noqeri/abi.h","public ABI v2 header"},
        {"Doc/ABI.md","ABI contract documentation"},{"Doc/FREESTANDING.md","freestanding target documentation"},{"Doc/LANGUAGE_REFERENCE.md","language reference"},{"Grammar/noqeri.grammar.md","grammar contract"},{"InternalDocs/ARCHITECTURE.md","repository architecture"},
        {"Compiler/core.cpp","compiler core"},{"Compiler/modules.cpp","module graph"},{"Compiler/type_checker.cpp","type checker"},{"Compiler/nir.cpp","NIR implementation"},{"Compiler/optimizer.cpp","optimizer"},{"Compiler/native_backend.cpp","native backend"},{"Compiler/linker.cpp","tool process/target adapter"},{"Compiler/semantic_rules.nqr","Noqeri semantic-rule seed"},
        {"Compiler/bootstrap/lexer_host.cpp","bootstrap lexer"},{"Compiler/bootstrap/parser_host.cpp","bootstrap parser"},{"Compiler/bootstrap/abi_runtime_host.cpp","bootstrap ABI runtime"},{"Compiler/bootstrap/interpreter_host.cpp","bootstrap interpreter"},{"Compiler/bootstrap/cli_host.cpp","bootstrap CLI"},{"Compiler/bootstrap/lsp_host.cpp","bootstrap LSP"},{"Compiler/bootstrap/formatter_host.cpp","bootstrap formatter"},{"Compiler/bootstrap/package_host.cpp","bootstrap package tooling"},{"Compiler/bootstrap/test_runner_host.cpp","bootstrap test runner"},{"Compiler/bootstrap/noqeridb_host.cpp","NoqeriDB host adapter"},{"Compiler/bootstrap/security_audit_host.cpp","security-audit host adapter"},{"Compiler/bootstrap/production_doctor_host.cpp","production doctor host adapter"},
        {"Parser/ascii.nqr","Noqeri lexer support"},{"Parser/token_rules.nqr","Noqeri token rules"},{"Runtime/status.nqr","Noqeri runtime status"},{"Runtime/memory.nqr","Noqeri runtime memory"},{"Runtime/limits.nqr","Noqeri runtime limits"},
        {"Programs/selftest.nqr","Noqeri self-test program"},{"Programs/diagnostics.nqr","Noqeri diagnostics policy"},{"Programs/version.nqr","Noqeri version policy"},
        {"Modules/builtin.nqr","Noqeri built-in module helpers"},{"Modules/native.nqr","Noqeri native-module contract"},{"Objects/value.nqr","Noqeri value model"},{"Objects/text.nqr","Noqeri text/byte model"},{"Database/noqeridb.nqr","Noqeri database core"},
        {"Lib/std/int.nqr","Noqeri integer helpers"},{"Lib/std/slice_i64.nqr","Noqeri slice algorithms"},{"Lib/std/search.nqr","Noqeri search algorithms"},{"Lib/std/sort.nqr","Noqeri sort algorithms"},{"Lib/std/stats.nqr","Noqeri statistics helpers"},{"Lib/std/bytes.nqr","Noqeri byte helpers"},{"Lib/std/utf8.nqr","Noqeri UTF-8 helpers"},{"Lib/std/checksum.nqr","Noqeri checksum helpers"},{"Lib/std/memory.nqr","Noqeri memory helpers"},{"Lib/std/status.nqr","Noqeri status helpers"},{"Lib/std/time.nqr","Noqeri time helpers"},{"Lib/std/random.nqr","Noqeri deterministic random helpers"},{"Lib/std/matrix_i64.nqr","Noqeri matrix helpers"},{"Lib/std/range.nqr","Noqeri range helpers"},{"Lib/std/text.nqr","Noqeri text helpers"},{"Lib/std/set_i64.nqr","Noqeri set helpers"},{"Lib/std/map_i64.nqr","Noqeri map helpers"},{"Lib/std/stack_i64.nqr","Noqeri stack helpers"},{"Lib/std/queue_i64.nqr","Noqeri queue helpers"},{"Lib/std/algorithm.nqr","Noqeri general algorithms"},{"Lib/std/bit.nqr","Noqeri bit helpers"},{"Lib/std/validation.nqr","Noqeri validation helpers"},{"Lib/std/window.nqr","Noqeri window helpers"},{"Lib/std/pair_i64.nqr","Noqeri pair helpers"},{"Lib/std/counter.nqr","Noqeri counter helpers"},{"Lib/std/compare.nqr","Noqeri comparison helpers"},{"Lib/security/memory.nqr","Noqeri security memory helpers"},{"Lib/test/assert.nqr","Noqeri test assertions"},
        {"Platforms/capabilities.nqr","Noqeri platform capability model"},{"Platforms/target_select.nqr","Noqeri target selection policy"},{"PC/platform.nqr","Noqeri Windows capability model"},{"Mac/platform.nqr","Noqeri macOS capability model"},{"Android/platform.nqr","Noqeri Android capability model"},{"PCbuild/profile.nqr","Noqeri build profile model"},
        {"Tools/security/policy.nqr","Noqeri security policy helpers"},{"Tools/fuzz/generator.nqr","Noqeri fuzz generator"},{"Tools/build/policy.nqr","Noqeri build policy"},{"Tools/scripts/source_floor.nqr","Noqeri source ownership floor"},
        {"Benchmarks/stdlib.nqr","Noqeri standard-library benchmark"},{"tests/noqeri_owned_components.nqr","Noqeri-owned component integration test"},{"tests/noqeri_library_suite.nqr","expanded Noqeri library integration test"},{"examples/ecosystem_smoke.nqr","Noqeri ecosystem smoke program"},
        {"editors/vscode/package.json","VS Code language package"},{"scripts/build.sh","POSIX build script"},{"scripts/build.ps1","Windows build script"},{"scripts/verify_repository.sh","POSIX repository verifier"},{"scripts/verify_repository.ps1","Windows repository verifier"},{"examples/hello.nqr","smoke example"}
    };
    int failures=0;
    std::cout<<"noqeri production-track doctor\ncompiler="<<NOQERI_COMPILER_VERSION<<"\nlanguage="<<NOQERI_LANGUAGE_VERSION<<"\nedition="<<NOQERI_EDITION<<"\nabi="<<NOQERI_ABI_VERSION<<"\ndefault-target="<<NOQERI_DEFAULT_TARGET<<"\ntargets="<<NOQERI_SUPPORTED_TARGETS<<"\n";
    for(const auto&check:required){auto full=root/check.path;if(std::filesystem::exists(full))std::cout<<"ok   "<<check.label<<" ("<<check.path.string()<<")\n";else{++failures;std::cerr<<"fail "<<check.label<<" missing: "<<full.string()<<"\n";}}
    const std::vector<std::filesystem::path>forbidden={
        root/"project.noe",root/"noe.lock",root/"project.ric",root/"ric.lock",root/"Include/noe",root/"Include/ric",root/"Grammar/noe.grammar.md",root/"Grammar/ric.grammar.md",
        root/"Modules/README.md",root/"Objects/README.md",root/"Lib/README.md",root/"Lib/test/README.md",root/"Android/README.md",root/"Mac/README.md",root/"PC/README.md",root/"PCbuild/README.md",root/"Platforms/README.md",root/"Parser/README.md",root/"Runtime/README.md",root/"Programs/README.md",root/"Benchmarks/README.md",root/"Compiler/README.md",root/"Tools/README.md",root/"Tools/build/README.md",root/"Tools/fuzz/README.md",
        root/"Parser/lexer.cpp",root/"Parser/parser.cpp",root/"Runtime/abi.cpp",root/"Runtime/interpreter.cpp",root/"Programs/noqeri.cpp",root/"Programs/lsp.cpp",root/"Programs/formatter.cpp",root/"Programs/package.cpp",root/"Programs/test_runner.cpp",root/"Tools/build/production.cpp"
    };
    for(const auto&path:forbidden)if(std::filesystem::exists(path)){++failures;std::cerr<<"fail obsolete/bootstrap-leak path present: "<<path.string()<<"\n";}
    const std::vector<std::filesystem::path>noqeriOwned={root/"Modules",root/"Objects",root/"Lib",root/"Database",root/"Parser",root/"Runtime",root/"Programs",root/"Tools/security",root/"Tools/fuzz",root/"Tools/build",root/"Platforms",root/"PC",root/"Mac",root/"Android",root/"PCbuild"};
    for(const auto&dir:noqeriOwned){
        if(!std::filesystem::exists(dir))continue;
        for(const auto&e:std::filesystem::recursive_directory_iterator(dir)){
            if(!e.is_regular_file())continue;
            const auto ext=e.path().extension().string();
            if(ext==".c"||ext==".cc"||ext==".cpp"||ext==".h"||ext==".hpp"){
                ++failures;std::cerr<<"fail C/C++ source in Noqeri-owned implementation area: "<<e.path().string()<<"\n";
            }
        }
    }
    std::size_t noqeriFiles=0,stdModules=0;
    if(std::filesystem::exists(root))for(const auto&e:std::filesystem::recursive_directory_iterator(root)){
        if(!e.is_regular_file())continue;
        const auto ext=e.path().extension().string();
        if(ext==".nqr")++noqeriFiles;
        // .noe is the supported NoeSQL source extension; only historical .ric is obsolete.
        if(ext==".ric"){++failures;std::cerr<<"fail legacy source extension present: "<<e.path().string()<<"\n";}
    }
    const auto stdRoot=root/"Lib/std";
    if(std::filesystem::exists(stdRoot))for(const auto&e:std::filesystem::recursive_directory_iterator(stdRoot))if(e.is_regular_file()&&e.path().extension()==".nqr")++stdModules;
    if(noqeriFiles<30){++failures;std::cerr<<"fail Noqeri source floor: expected at least 30 .nqr files, found "<<noqeriFiles<<"\n";}
    if(stdModules<16){++failures;std::cerr<<"fail standard-library floor: expected at least 16 .nqr modules, found "<<stdModules<<"\n";}
    std::cout<<"source-floor noqeri="<<noqeriFiles<<" stdlib="<<stdModules<<"\n";
    if(failures==0){std::cout<<"noqeri production-track repository checks passed for language "<<NOQERI_LANGUAGE_VERSION<<" and ABI v"<<NOQERI_ABI_VERSION<<".\n";return 0;}
    std::cerr<<"noqeri production-track repository checks failed: "<<failures<<" issue(s).\n";return 1;
}

} // namespace noe
