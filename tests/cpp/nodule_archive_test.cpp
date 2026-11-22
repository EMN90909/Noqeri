#include "noe.hpp"
#include "targets.hpp"
#include "nodule_archive.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool requireCond(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "nodule_archive_test FAILED: " << msg << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    noqeri_nodule::NdlManifest manifest;
    manifest.name = "demo/mathpkg";
    manifest.version = "1.2.0";
    manifest.languageVersion = "0.1.5.1";
    manifest.abiVersion = 2;
    manifest.entry = "src/main.noqeri";
    manifest.exports = {"src/main.noqeri", "src/helper.noqeri"};

    const std::string helperSrc =
        "export function addMul(a: int, b: int): int {\n"
        "    return a * 3 + b\n"
        "}\n";
    const std::string mainSrc =
        "import \"./helper.noqeri\"\n"
        "export function compute(): int {\n"
        "    return addMul(7, 5)\n"
        "}\n"
        "print(compute())\n";

    const std::vector<std::pair<std::string, std::string>> files = {
        {"src/helper.noqeri", helperSrc},
        {"src/main.noqeri", mainSrc},
    };

    const std::string validBytes = noqeri_nodule::buildNdlArchive(manifest, files);

    // 1. In-memory verify & findEntry
    noqeri_nodule::NdlArchive archive;
    std::string err;
    if (!requireCond(noqeri_nodule::loadNdlArchiveFromBytes(validBytes, archive, err),
                     ("valid archive bytes should load: " + err).c_str()))
        return 1;
    if (!requireCond(archive.manifest.name == "demo/mathpkg" &&
                         archive.manifest.abiVersion == 2 &&
                         archive.findEntry("src/helper.noqeri") != nullptr &&
                         archive.findEntry("src/main.noqeri") != nullptr,
                     "loaded archive manifest and entries match"))
        return 1;

    // 2. ModuleGraph in-memory load & both backends (LLVM AOT + Direct Native)
    {
        noe::ModuleGraph graph;
        noe::Diagnostics diag;
        if (!requireCond(graph.loadNdlBuffer("virtual/mathpkg.ndl", validBytes, diag) && !diag.hasErrors(),
                         "ModuleGraph::loadNdlBuffer succeeds on valid .ndl"))
            return 1;
        noe::Program merged = graph.mergedProgram();
        noe::CompileOptions opts;
        opts.optimization = noe::OptimizationLevel::O3;
        auto compiled = noe::compileProgram(merged, diag, opts);
        if (!requireCond(!compiled.diagnostics.hasErrors() && !diag.hasErrors(),
                         "compileProgram on .ndl mergedProgram succeeds"))
            return 1;

        const fs::path tmpOut = fs::temp_directory_path() / "noqeri_ndl_test_out";
        fs::create_directories(tmpOut);
        const fs::path cOut = tmpOut / "mathpkg.c";
        const fs::path asmOut = tmpOut / "mathpkg.s";
        if (!requireCond(noe::PortableCBackend{}.emitSource(compiled.nir, cOut, diag) && !diag.hasErrors(),
                         "PortableCBackend emits C for .ndl module"))
            return 1;
        if (!requireCond(noe::NativeBackend{}.emitAssembly(compiled.nir, asmOut, diag) && !diag.hasErrors(),
                         "NativeBackend emits x86-64 asm for .ndl module"))
            return 1;
        fs::remove_all(tmpOut);
    }

    // 3. Disk .ndl archive import resolution via ModuleGraph + symlink rejection
    {
        const fs::path workDir = fs::temp_directory_path() / "noqeri_ndl_disk_test";
        fs::remove_all(workDir);
        fs::create_directories(workDir / "nodules");
        const fs::path ndlFile = workDir / "nodules" / "mathpkg.ndl";
        {
            std::ofstream out(ndlFile, std::ios::binary | std::ios::trunc);
            out.write(validBytes.data(), static_cast<std::streamsize>(validBytes.size()));
        }
        const fs::path appFile = workDir / "app.noqeri";
        {
            std::ofstream out(appFile, std::ios::trunc);
            out << "import \"mathpkg\"\nprint(compute())\n";
        }
        noe::ModuleGraph graph;
        noe::Diagnostics diag;
        if (!requireCond(graph.load(appFile, diag) && !diag.hasErrors(),
                         "ModuleGraph resolves .ndl module from project nodules directory"))
            return 1;
        noe::CompileOptions opts;
        opts.optimization = noe::OptimizationLevel::O3;
        auto compiled = noe::compileProgram(graph.mergedProgram(), diag, opts);
        if (!requireCond(!compiled.diagnostics.hasErrors() && !diag.hasErrors(),
                         "imported .ndl module compiles to NIR"))
            return 1;

#if !defined(_WIN32)
        const fs::path linkPath = workDir / "nodules" / "link.ndl";
        std::error_code ec;
        fs::create_symlink(ndlFile, linkPath, ec);
        if (!ec) {
            noqeri_nodule::NdlArchive symArc;
            std::string symErr;
            if (!requireCond(!noqeri_nodule::loadNdlArchiveFromFile(linkPath, symArc, symErr),
                             "symlink .ndl path must be rejected"))
                return 1;
        }
#endif
        fs::remove_all(workDir);
    }

    // 4. Tampered byte MUST fail verification and ModuleGraph load
    {
        std::string tampered = validBytes;
        tampered[tampered.size() - 5] ^= static_cast<char>(0x01);
        noqeri_nodule::NdlArchive badArc;
        std::string badErr;
        if (!requireCond(!noqeri_nodule::loadNdlArchiveFromBytes(tampered, badArc, badErr),
                         "tampered payload byte must fail loadNdlArchiveFromBytes"))
            return 1;

        noe::ModuleGraph badGraph;
        noe::Diagnostics badDiag;
        if (!requireCond(!badGraph.loadNdlBuffer("tampered.ndl", tampered, badDiag) && badDiag.hasErrors(),
                         "tampered .ndl buffer must fail ModuleGraph with diagnostics"))
            return 1;
    }

    // 5. Bad magic MUST fail immediately
    {
        std::string badMagic = validBytes;
        badMagic[0] = 'X';
        noqeri_nodule::NdlArchive badArc;
        std::string badErr;
        if (!requireCond(!noqeri_nodule::loadNdlArchiveFromBytes(badMagic, badArc, badErr),
                         "bad magic header must fail"))
            return 1;
    }

    // 6. Wrong ABI / language version gate MUST fail
    {
        auto wrongAbi = manifest;
        wrongAbi.abiVersion = 1;
        const std::string wrongAbiBytes = noqeri_nodule::buildNdlArchive(wrongAbi, files);
        noqeri_nodule::NdlArchive badArc;
        std::string badErr;
        if (!requireCond(!noqeri_nodule::loadNdlArchiveFromBytes(wrongAbiBytes, badArc, badErr),
                         "ABI v1 archive must fail ABI v2 gate"))
            return 1;

        auto wrongLang = manifest;
        wrongLang.languageVersion = "0.1.4";
        const std::string wrongLangBytes = noqeri_nodule::buildNdlArchive(wrongLang, files);
        if (!requireCond(!noqeri_nodule::loadNdlArchiveFromBytes(wrongLangBytes, badArc, badErr),
                         "0.1.4 language version archive must fail 0.1.5 gate"))
            return 1;
    }

    // 7. Unsafe paths (.., leading /, drive prefix) MUST be rejected
    {
        for (const char* unsafePath : {"../escape.noqeri", "/abs/path.noqeri", "C:drive.noqeri", "a\\b.noqeri"}) {
            bool threw = false;
            try {
                noqeri_nodule::buildNdlArchive(manifest, {{unsafePath, "let x = 1\n"}});
            } catch (const std::exception&) {
                threw = true;
            }
            if (!requireCond(threw, "unsafe entry path must be rejected"))
                return 1;
        }
    }

    return 0;
}
