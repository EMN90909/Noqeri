#include "noe.hpp"
#include <iostream>
namespace noe {
int TestRunner::runDirectory(const std::filesystem::path& dir) const {
    if (!std::filesystem::exists(dir)) {
        std::cerr << "NQR-TEST7000: test directory not found: " << dir << "\n";
        return 1;
    }
    int failed = 0, total = 0;
    for (auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".noqeri") continue;
        const auto name = e.path().filename().string();
        if (name == "project.noqeri" || name.rfind("noesql_", 0) == 0) continue;
        ++total;
        const bool isRuntimeTrap =
            name.find("out_of_bounds") != std::string::npos ||
            name.find("empty_pop") != std::string::npos ||
            name.find("oob") != std::string::npos ||
            name.find("_trap") != std::string::npos ||
            name.find("float_cast_invalid") != std::string::npos ||
            name == "portable_native_invalid_cast.noqeri" ||
            name == "beginner_growable_bad_index_set_range.noqeri";
        const bool expectCompileError =
            !isRuntimeTrap &&
            (name.find("invalid") != std::string::npos ||
             name.find("bad") != std::string::npos ||
             name == "beginner_growable_fixed_array.noqeri");
        const bool isCheckOnly =
            name == "native_mailbox_link.noqeri" ||
            name == "owned_collections_native.noqeri" ||
            name == "owned_generic_native.noqeri";
        try {
            CompileOptions opts;
            auto c = compileFile(e.path(), opts);
            if (!c.diagnostics.hasErrors() && e.path().parent_path().filename() == "web_smoke") {
                WebBackend().emitModule(c.ast, c.diagnostics);
            }
            if (expectCompileError) {
                if (c.diagnostics.hasErrors()) {
                    std::cout << "PASS " << e.path() << "\n";
                } else {
                    ++failed;
                    std::cerr << "FAIL (expected compile error) " << e.path() << "\n";
                }
            } else if (c.diagnostics.hasErrors()) {
                ++failed;
                std::cerr << "FAIL " << e.path() << "\n";
                c.diagnostics.print(e.path().string());
            } else if (isCheckOnly) {
                std::cout << "PASS " << e.path() << "\n";
            } else {
                Interpreter i;
                int rc = i.run(c.nir);
                if (isRuntimeTrap) {
                    if (rc != 0) std::cout << "PASS " << e.path() << "\n";
                    else { ++failed; std::cerr << "FAIL (expected runtime trap) " << e.path() << "\n"; }
                } else if (rc) {
                    ++failed;
                    std::cerr << "FAIL " << e.path() << "\n";
                } else {
                    std::cout << "PASS " << e.path() << "\n";
                }
            }
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "FAIL " << e.path() << ": " << ex.what() << "\n";
        }
    }
    std::cout << "noqeri tests: " << (total - failed) << " passed, " << failed << " failed, " << total << " total\n";
    return failed ? 1 : 0;
}
} // namespace noe

