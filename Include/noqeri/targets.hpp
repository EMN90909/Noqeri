#pragma once
#include "diagnostics.hpp"
#include "nir.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace noe {

enum class Architecture { Unknown, X86_64, AArch64, Wasm32 };
enum class ObjectFormat { None, Elf64, Coff64, MachO64, Wasm, Assembly };
struct TargetTriple { Architecture architecture=Architecture::Unknown; std::string vendor="unknown"; std::string environment="none"; ObjectFormat objectFormat=ObjectFormat::None; static TargetTriple parse(const std::string& text); std::string str() const; bool isFreestanding() const { return environment=="none" || environment=="unknown" || environment=="uefi"; } };
struct TargetInfo { TargetTriple triple; std::size_t pointerWidth=8; std::size_t stackAlignment=16; bool littleEndian=true; };
class TargetRegistry { public: static TargetInfo resolve(const std::string& triple); static std::vector<std::string> supported(); };
struct ProcessResult { int exitCode=-1; bool launched=false; };
class ProcessRunner { public: ProcessResult run(const std::filesystem::path& executable,const std::vector<std::string>& args,Diagnostics& diagnostics) const; };
class NativeBackend { public: bool emitAssembly(const NirProgram& program,const std::filesystem::path& output,Diagnostics& diagnostics,bool uefiFreestanding=false) const; };
// Checked C11 AOT bridge; host C compiler emits platform-native machine code
// for a deliberately restricted, typed scalar NIR subset.
class PortableCBackend {
public:
    bool emitSource(const NirProgram& program,const std::filesystem::path& output,
                    Diagnostics& diagnostics) const;
};
class AssemblerDriver { public: bool assemble(const std::filesystem::path& assembly,const std::filesystem::path& output,const TargetTriple& target,Diagnostics& diagnostics) const; };
class LinkerDriver { public: bool link(const std::filesystem::path& assembly,const std::filesystem::path& output,Diagnostics& diagnostics) const; bool linkObjects(const std::vector<std::filesystem::path>& objects,const std::filesystem::path& output,const TargetTriple& target,Diagnostics& diagnostics) const; };

} // namespace noe
