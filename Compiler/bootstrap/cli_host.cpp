#include "noe.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace noe;

namespace {
void usage() {
    std::cout << "noqeri " << NOQERI_COMPILER_VERSION << "\nusage: noqeri <command> [options]\n\n"
              << "  new <name> [dir]                 create a Noqeri project\n"
              << "  lex <file>                        print lexer tokens\n"
              << "  check [file] [-O...]              parse and type-check module graph\n"
              << "  nir [file] [-O...]                lower module graph to typed NIR\n"
              << "  run [file] [-O...]                execute through reference runtime\n"
              << "  build [file] [assembly] [-O...]   emit target assembly\n"
              << "  web [file] [module.nqo]           emit checked portable Noqeri web module\n"
              << "  db <script.nqd> [data.nqdb]       execute a transactional NoqeriDB script\n"
              << "  stress [dir] [--full]             stress-test compiler, libraries and NoqeriDB\n"
              << "  assemble <asm> <object> [target]  run assembler adapter without a shell\n"
              << "  link <output> <object...>          run explicit linker adapter\n"
              << "  targets                           list compiler target contracts\n"
              << "  format <file>                     canonical-format source in place\n"
              << "  manifest [file]                   read project.nqr\n"
              << "  lock [file]                       generate content-addressed noqeri.lock\n"
              << "  audit [dir]                       verify lock integrity and review capabilities\n"
              << "  test [dir]                        compile and run .nqr tests\n"
              << "  doctor [dir]                      verify repository health\n"
              << "  release-check [dir]               repository production checks\n"
              << "  lsp                               run JSON-RPC language server\n\n"
              << "optimization: -O0 -O1 -O2 -O3 -Os -Oz\n";
}

std::optional<OptimizationLevel> parseOptimization(const std::string& arg) {
    if (arg == "-O0") return OptimizationLevel::O0;
    if (arg == "-O1") return OptimizationLevel::O1;
    if (arg == "-O2") return OptimizationLevel::O2;
    if (arg == "-O3") return OptimizationLevel::O3;
    if (arg == "-Os") return OptimizationLevel::Os;
    if (arg == "-Oz") return OptimizationLevel::Oz;
    return std::nullopt;
}

CompileOptions optionsFromArgs(int argc, char** argv, const std::string& target) {
    CompileOptions options;
    options.target = target.empty() ? NOQERI_DEFAULT_TARGET : target;
    for (int i = 2; i < argc; ++i) {
        if (auto level = parseOptimization(argv[i])) options.optimization = *level;
    }
    return options;
}

struct SourceSelection {
    std::filesystem::path source;
    std::string projectName;
    std::string target = NOQERI_DEFAULT_TARGET;
};

std::optional<SourceSelection> selectSource(int argc, char** argv, Diagnostics& d) {
    if (argc >= 3 && argv[2][0] != '-') {
        return SourceSelection{argv[2], std::filesystem::path(argv[2]).stem().string(), NOQERI_DEFAULT_TARGET};
    }
    auto manifest = PackageManager{}.loadManifest("project.nqr", d);
    if (!manifest) return std::nullopt;
    return SourceSelection{manifest->entry, manifest->name, manifest->target};
}

bool isIgnoredDirectory(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == ".git" || name == ".github" || name == ".vs" || name == "build" ||
           name == "out" || name == "dist" || name == "node_modules" || name == ".cache";
}

bool isOrdinaryNoqeriSource(const std::filesystem::path& path) {
    const auto file = path.filename().string();
    if (path.extension() != ".nqr") return false;
    if (file == "project.nqr") return false;
    if (file == "fuzz.nqr") return false;
    return true;
}

std::string relativeText(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    auto rel = std::filesystem::relative(path, root, ec);
    return ec ? path.string() : rel.string();
}

struct StressStats {
    int passed = 0;
    int failed = 0;
};

bool checkFileForStress(const std::filesystem::path& root, const std::filesystem::path& path, bool verbose) {
    CompileOptions options;
    auto result = compileFile(path, options);
    if (result.diagnostics.hasErrors()) {
        std::cout << "fail check " << relativeText(root, path) << "\n";
        result.diagnostics.print(path.string());
        return false;
    }
    if (verbose) std::cout << "ok   check " << relativeText(root, path) << "\n";
    return true;
}

bool runProgramForStress(const std::filesystem::path& root, const std::filesystem::path& path) {
    CompileOptions options;
    auto result = compileFile(path, options);
    if (result.diagnostics.hasErrors()) {
        std::cout << "fail run-check " << relativeText(root, path) << "\n";
        result.diagnostics.print(path.string());
        return false;
    }
    const int code = Interpreter{}.run(result.nir);
    if (code != 0) {
        std::cout << "fail run " << relativeText(root, path) << " exit=" << code << "\n";
        return false;
    }
    std::cout << "ok   run " << relativeText(root, path) << "\n";
    return true;
}

bool runNoqeriDbStress(const std::filesystem::path& root) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("noqeri-stress-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    const auto script = dir / "stress.nqd";
    const auto database = dir / "stress.nqdb";
    {
        std::ofstream out(script, std::ios::trunc);
        out << "table users { id: int key, name: text required, active: bool required }\n"
            << "delete users\n"
            << "insert users { id: 1, name: \"Ada\", active: true }\n"
            << "insert users { id: 2, name: \"Linus\", active: true }\n"
            << "update users set { name: \"Ada Lovelace\" } where id = 1\n"
            << "select users where active = true\n";
    }
    Diagnostics d;
    std::ostringstream output;
    const bool ok = NoqeriDatabase{}.execute(script, database, output, d);
    if (!ok) {
        std::cout << "fail db fresh temp database\n";
        d.print(script.string());
    } else if (output.str().find("Ada Lovelace") == std::string::npos || output.str().find("Linus") == std::string::npos) {
        std::cout << "fail db output missing expected rows\n" << output.str();
    } else {
        std::cout << "ok   db fresh temp database\n";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        return true;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    (void)root;
    return false;
}

bool runParserFuzz(const std::filesystem::path& root, int cases) {
    const std::string chars = "(){}[]<>+-*/=abcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937 rng(0x4E514552u);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(chars.size() - 1));
    const auto dir = std::filesystem::temp_directory_path() / "noqeri-stress-fuzz";
    std::filesystem::create_directories(dir);
    bool ok = true;
    for (int i = 0; i < cases; ++i) {
        std::string text;
        text.reserve(500);
        for (int j = 0; j < 500; ++j) text.push_back(chars[static_cast<std::size_t>(pick(rng))]);
        const auto file = dir / ("fuzz-" + std::to_string(i) + ".nqr");
        { std::ofstream out(file, std::ios::trunc); out << text << "\n"; }
        try {
            CompileOptions options;
            (void)compileFile(file, options);
        } catch (const std::exception& e) {
            std::cout << "fail fuzz case " << i << ": " << e.what() << "\n";
            ok = false;
            break;
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    (void)root;
    if (ok) std::cout << "ok   parser fuzz cases=" << cases << "\n";
    return ok;
}

int runStress(int argc, char** argv) {
    std::filesystem::path root = ".";
    bool full = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--full") full = true;
        else if (!arg.empty() && arg[0] != '-') root = arg;
    }
    root = std::filesystem::absolute(root);
    const auto started = std::chrono::steady_clock::now();
    StressStats stats;
    auto step = [&](const std::string& name, const auto& fn) {
        const auto before = std::chrono::steady_clock::now();
        bool ok = false;
        try { ok = fn(); }
        catch (const std::exception& e) { std::cout << "fail " << name << ": " << e.what() << "\n"; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
        if (ok) { ++stats.passed; std::cout << "pass " << name << " (" << elapsed << " ms)\n"; }
        else { ++stats.failed; std::cout << "FAIL " << name << " (" << elapsed << " ms)\n"; }
    };

    std::cout << "noqeri stress\nroot=" << root << "\nmode=" << (full ? "full" : "quick") << "\n";

    step("doctor", [&] { return runProductionDoctor(root.string()) == 0; });
    step("audit", [&] { return runSecurityAudit(root.string()) == 0; });
    step("test-directory", [&] { return TestRunner{}.runDirectory((root / "tests").string()) == 0; });
    step("noqeridb-temp", [&] { return runNoqeriDbStress(root); });

    step("source-sweep", [&] {
        int checked = 0;
        for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
            const auto& entry = *it;
            if (entry.is_directory() && isIgnoredDirectory(entry.path())) { it.disable_recursion_pending(); continue; }
            if (!entry.is_regular_file() || !isOrdinaryNoqeriSource(entry.path())) continue;
            if (!checkFileForStress(root, entry.path(), false)) return false;
            ++checked;
        }
        std::cout << "ok   checked ordinary .nqr source files=" << checked << "\n";
        return checked > 0;
    });

    const std::vector<std::filesystem::path> smokePrograms = {
        root / "examples" / "hello.nqr",
        root / "examples" / "ecosystem_smoke.nqr",
        root / "Programs" / "selftest.nqr",
        root / "tests" / "noqeri_library_suite.nqr",
        root / "tests" / "short_circuit.nqr"
    };
    step("runtime-smoke", [&] {
        for (const auto& file : smokePrograms) if (!runProgramForStress(root, file)) return false;
        return true;
    });

    step("repeat-check", [&] {
        const int count = full ? 1000 : 100;
        const auto file = root / "examples" / "hello.nqr";
        for (int i = 0; i < count; ++i) if (!checkFileForStress(root, file, false)) return false;
        std::cout << "ok   repeated hello checks=" << count << "\n";
        return true;
    });

    step("parser-fuzz", [&] { return runParserFuzz(root, full ? 1000 : 100); });

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "\nNoqeri stress summary\n"
              << "passed=" << stats.passed << "\n"
              << "failed=" << stats.failed << "\n"
              << "time-ms=" << elapsed << "\n";
    if (stats.failed == 0) {
        std::cout << "result=ok\n";
        return 0;
    }
    std::cout << "result=failed\n";
    return 1;
}
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 0; }
    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "version") {
        std::cout << "noqeri " << NOQERI_COMPILER_VERSION << "\nlanguage=" << NOQERI_LANGUAGE_VERSION
                  << "\nedition=" << NOQERI_EDITION << "\nabi=" << NOQERI_ABI_VERSION
                  << "\ndefault-target=" << NOQERI_DEFAULT_TARGET << "\npackage-format=" << NOQERI_PACKAGE_FORMAT
                  << "\nregistry-protocol=" << NOQERI_REGISTRY_PROTOCOL << "\n";
        return 0;
    }
    if (cmd == "stress" || cmd == "--stress") return runStress(argc, argv);
    if (cmd == "targets") {
        for (const auto& target : TargetRegistry::supported()) {
            auto info = TargetRegistry::resolve(target);
            std::cout << target << " pointer=" << (info.pointerWidth * 8) << " stack-align=" << info.stackAlignment
                      << (info.triple.isFreestanding() ? " freestanding" : " hosted-adapter") << "\n";
        }
        return 0;
    }
    if (cmd == "lsp") return LanguageServer{}.run();
    if (cmd == "doctor" || cmd == "release-check") return runProductionDoctor(argc >= 3 ? argv[2] : ".");
    if (cmd == "audit") return runSecurityAudit(argc >= 3 ? argv[2] : ".");
    if (cmd == "db") {
        if (argc < 3) { std::cerr << "NQR-D8041: db requires a .nqd script\n"; return 1; }
        Diagnostics d;
        std::filesystem::path overridePath = argc >= 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{};
        if (!NoqeriDatabase{}.execute(argv[2], overridePath, std::cout, d)) { d.print(argv[2]); return 1; }
        return 0;
    }
    try {
        if (cmd == "assemble") {
            if (argc < 4) { usage(); return 1; }
            const auto target = TargetTriple::parse(argc >= 5 ? argv[4] : NOQERI_DEFAULT_TARGET);
            Diagnostics d;
            if (!AssemblerDriver{}.assemble(argv[2], argv[3], target, d)) { d.print(argv[2]); return 1; }
            std::cout << "assembled target object: " << argv[3] << " target=" << target.str() << "\n";
            return 0;
        }
        if (cmd == "link") {
            if (argc < 4) { usage(); return 1; }
            std::vector<std::filesystem::path> objects;
            for (int i = 3; i < argc; ++i) objects.emplace_back(argv[i]);
            Diagnostics d;
            if (!LinkerDriver{}.linkObjects(objects, argv[2], TargetTriple::parse(NOQERI_DEFAULT_TARGET), d)) { d.print(argv[2]); return 1; }
            std::cout << "linked artifact: " << argv[2] << "\n";
            return 0;
        }
        if (cmd == "new") {
            if (argc < 3) { std::cerr << "NQR-C0002: new requires a project name\n"; return 1; }
            std::filesystem::path dir = argc >= 4 ? argv[3] : argv[2];
            Diagnostics d;
            if (!PackageManager{}.createProject(dir, argv[2], d)) { d.print(dir.string()); return 1; }
            std::cout << "created Noqeri project " << dir << "\n";
            return 0;
        }
        if (cmd == "test") return TestRunner{}.runDirectory(argc >= 3 ? argv[2] : "tests");
        if (cmd == "manifest" || cmd == "lock") {
            std::filesystem::path path = argc >= 3 ? argv[2] : "project.nqr";
            Diagnostics d;
            auto p = PackageManager{}.loadManifest(path, d);
            if (!p) { d.print(path.string()); return 1; }
            if (cmd == "manifest") {
                std::cout << "name=" << p->name << "\nversion=" << p->version << "\nedition=" << p->edition
                          << "\nentry=" << p->entry << "\nprofile=" << p->profile << "\ntarget=" << p->target
                          << "\nregistry=" << p->registry << "\ndependencies=" << p->dependencySpecs.size() << "\n";
                return 0;
            }
            auto lock = path.parent_path() / "noqeri.lock";
            if (!PackageManager{}.writeLock(*p, lock, d)) { d.print(path.string()); return 1; }
            std::cout << "wrote " << lock << "\n";
            return 0;
        }
        if (cmd == "format") {
            if (argc < 3) { usage(); return 1; }
            std::filesystem::path path = argv[2];
            auto source = readTextFile(path);
            Diagnostics d;
            auto text = Formatter{}.format(source, d);
            if (d.hasErrors()) { d.print(path.string()); return 1; }
            std::ofstream out(path, std::ios::trunc);
            out << text;
            std::cout << "formatted " << path << "\n";
            return 0;
        }
        Diagnostics selectionDiagnostics;
        auto selected = selectSource(argc, argv, selectionDiagnostics);
        if (!selected) { selectionDiagnostics.print("project.nqr"); return 1; }
        auto path = selected->source;
        if (cmd == "lex") {
            auto source = readTextFile(path);
            Diagnostics d;
            Lexer lexer(source, d);
            auto tokens = lexer.lex();
            if (d.hasErrors()) { d.print(path.string()); return 1; }
            for (const auto& t : tokens) std::cout << t.span.line << ':' << t.span.column << "  " << tokenKindName(t.kind) << "  " << t.lexeme << "\n";
            return 0;
        }
        auto options = optionsFromArgs(argc, argv, selected->target);
        auto compilation = compileFile(path, options);
        if (compilation.diagnostics.hasErrors()) { compilation.diagnostics.print(path.string()); return 1; }
        if (cmd == "check") {
            std::cout << "check succeeded: " << path << " modules=" << (compilation.modules ? compilation.modules->modules().size() : 1) << " target=" << options.target << "\n";
            return 0;
        }
        if (cmd == "nir") { std::cout << printNir(compilation.nir); return 0; }
        if (cmd == "run") return Interpreter{}.run(compilation.nir);
        if (cmd == "web") {
            std::filesystem::path output = argc >= 4 && argv[3][0] != '-' ? std::filesystem::path(argv[3]) : std::filesystem::path("build") / (selected->projectName + ".nqo");
            if (output.extension().empty()) output += ".nqo";
            std::filesystem::create_directories(output.parent_path().empty() ? std::filesystem::path(".") : output.parent_path());
            Diagnostics d;
            if (!WebBackend{}.emitModule(compilation.ast, output, d)) { d.print(path.string()); return 1; }
            std::cout << "emitted Noqeri web object: " << output << "\n";
            return 0;
        }
        if (cmd == "build") {
            const auto target = TargetRegistry::resolve(options.target);
            if (target.triple.architecture != Architecture::X86_64) {
                std::cerr << "NQR-K5120: native backend currently implements x86_64 lowering; target contract " << options.target << " is recognized but has no backend yet\n";
                return 1;
            }
            std::filesystem::path output = argc >= 4 && argv[3][0] != '-' ? std::filesystem::path(argv[3]) : std::filesystem::path("build") / (selected->projectName + ".s");
            if (output.extension().empty()) output += ".s";
            Diagnostics d;
            if (!NativeBackend{}.emitAssembly(compilation.nir, output, d)) { d.print(path.string()); return 1; }
            std::cout << "emitted Noqeri x86-64 assembly: " << output << "\ntarget=" << options.target << "\nentry=noqeri_entry(noqeri_abi*)\n";
            return 0;
        }
        usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NQR-C0001: " << e.what() << "\n";
        return 1;
    }
}
