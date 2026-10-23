#pragma once
#include "nir.hpp"
#include <string>
#include <vector>

namespace noe {
// A restricted pre-link IR combiner. Imported modules must have no top-level
// initializer/effects. This is a foundation for cross-module optimization,
// not a .nqri object file LTO linker or production ABI resolver.
struct LinkTimeResult {
    bool ok=false;
    std::string error;
    NirProgram program;
    std::size_t importedFunctions=0;
};
LinkTimeResult combineNirModules(
    const NirProgram& root, const std::vector<NirProgram>& imports);
}
