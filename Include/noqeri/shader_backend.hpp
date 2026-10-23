#pragma once
#include "ast.hpp"
#include "diagnostics.hpp"
#include <filesystem>
#include <string>

namespace noe {
// A deliberately restricted, typed Noqeri-to-WGSL shader backend. The
// generated output can be compiled by WebGPU, but is not evidence of hardware
// execution without a real WebGPU device and validation layer.
class ShaderBackend {
public:
    bool emitWgsl(const Program& program, const std::filesystem::path& output,
                  const std::string& stage, Diagnostics& diagnostics) const;
};
} // namespace noe
