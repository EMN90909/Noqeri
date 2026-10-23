#pragma once
#include "ast.hpp"
#include "diagnostics.hpp"
#include <filesystem>

namespace noe {
// Strict pure Noqeri capture compiler into the unprivileged PGAS arithmetic
// VM format. No native machine code upload, IO, pointers or FFI.
class PgasBytecodeBackend {
public:
    bool emitModule(const Program& program,
                    const std::filesystem::path& output,
                    Diagnostics& diagnostics) const;
};
} // namespace noe
