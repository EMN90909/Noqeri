#pragma once
#include "diagnostics.hpp"
#include "modules.hpp"
#include "nir.hpp"
#include <filesystem>

namespace noe {

// Portable NQRI bootstrap emitter used by the trusted C++ stage-0 compiler.
// It serializes the already type-checked/lowered NIR into the same fixed-width
// format consumed by the native nqri runtime. Compiler semantics remain in the
// frontend; this class is deliberately only a deterministic serializer.
class NqriBackend {
public:
    bool emitModule(const NirProgram& program,
                    const ModuleGraph* modules,
                    const std::filesystem::path& output,
                    Diagnostics& diagnostics) const;
};

} // namespace noe
