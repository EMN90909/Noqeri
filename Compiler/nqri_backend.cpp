#include "noe.hpp"
#include "nqri.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace noe {
namespace {

constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kHeaderBytes = 32;
constexpr std::size_t kFunctionBytes = 40;
constexpr std::size_t kInstructionBytes = 96;
constexpr std::uint32_t kFlagVolatile = 1;

struct SpanRef {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

struct StableInstruction {
    std::uint32_t opcode = 0;
    std::uint32_t typeCode = 0;
    std::uint32_t flags = 0;
    std::int64_t dest = -1;
    std::int64_t left = -1;
    std::int64_t right = -1;
    std::int64_t immediate = 0;
    std::uint64_t symbol = 0;
    std::uint64_t width = 0;
    std::uint64_t sourceStart = 0;
    std::uint64_t sourceEnd = 0;
    double floatImmediate = 0.0;
};

struct StableFunction {
    std::uint64_t symbol = 0;
    SpanRef name;
    std::uint32_t firstInstruction = 0;
    std::uint32_t instructionCount = 0;
    std::uint32_t parameterCount = 0;
    std::uint32_t flags = 0;
};

std::uint64_t symbolId(const std::string& text) {
    if (text.empty()) return 0;
    std::uint64_t hash = 2166136261ull;
    for (unsigned char c : text) {
        hash = (hash * 16777619ull + static_cast<std::uint64_t>(c) + 1ull) % 4294967291ull;
    }
    return hash + 1ull;
}

class SourcePool {
public:
    explicit SourcePool(const ModuleGraph* modules) {
        if (!modules) return;
        for (const auto& source : modules->sources()) {
            bytes_.append(source.text);
            if (bytes_.empty() || bytes_.back() != '\n') bytes_.push_back('\n');
        }
    }

    SpanRef append(const std::string& text) {
        if (bytes_.size() > std::numeric_limits<std::uint32_t>::max() ||
            text.size() > std::numeric_limits<std::uint32_t>::max() - bytes_.size()) {
            throw std::overflow_error("NQRI source pool exceeds 32-bit format limit");
        }
        const auto start = static_cast<std::uint64_t>(bytes_.size());
        bytes_.append(text);
        const auto end = static_cast<std::uint64_t>(bytes_.size());
        bytes_.push_back('\n');
        return {start, end};
    }

    const std::string& bytes() const { return bytes_; }

private:
    std::string bytes_;
};

std::uint32_t typeCode(const std::string& name) {
    if (name == "void") return 1;
    if (name == "bool") return 2;
    if (name == "int" || name == "int64") return 3;
    if (name == "float" || name == "float64" || name == "f64" || name == "f32") return 4;
    if (name == "string") return 5;
    if (name == "i8") return 6;
    if (name == "u8") return 7;
    if (name == "i16") return 8;
    if (name == "u16") return 9;
    if (name == "i32") return 10;
    if (name == "u32") return 11;
    if (name == "i64") return 12;
    if (name == "u64") return 13;
    if (name == "isize") return 14;
    if (name == "usize") return 15;
    if (!name.empty() && name[0] == '*') return 16;
    if (name.rfind("[]", 0) == 0) return 17;
    if (!name.empty() && name[0] == '[') return 18;
    return 19;
}

std::uint32_t binaryOpcode(const std::string& op) {
    if (op == "+") return 6;
    if (op == "-") return 7;
    if (op == "*") return 8;
    if (op == "/") return 9;
    if (op == "%") return 10;
    if (op == "==") return 13;
    if (op == "!=") return 14;
    if (op == "<") return 15;
    if (op == "<=") return 16;
    if (op == ">") return 17;
    if (op == ">=") return 18;
    return std::numeric_limits<std::uint32_t>::max();
}

std::size_t expansionCount(const NirInstruction& instruction) {
    if (instruction.op == NirOp::CheckNonNull) return 0;
    if (instruction.op == NirOp::Call) return instruction.args.size() + 1;
    if (instruction.op == NirOp::PtrOffset && instruction.args.size() == 1) return 2;
    return 1;
}

void appendParameterPrefix(const NirFunction& fn, std::vector<StableInstruction>& out) {
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const auto reg = static_cast<std::int64_t>(fn.nextReg + i);
        StableInstruction parameter;
        parameter.opcode = 47;
        parameter.dest = reg;
        parameter.symbol = symbolId(fn.params[i]);
        parameter.immediate = static_cast<std::int64_t>(i);
        out.push_back(parameter);

        StableInstruction store;
        store.opcode = 5;
        store.left = reg;
        store.symbol = symbolId(fn.params[i]);
        out.push_back(store);
    }
}

bool translateFunction(const NirFunction& fn,
                       std::uint32_t globalFirst,
                       SourcePool& source,
                       std::vector<StableInstruction>& out,
                       Diagnostics& diagnostics) {
    std::vector<std::uint32_t> pcMap(fn.code.size() + 1, 0);
    std::size_t local = fn.params.size() * 2;
    for (std::size_t pc = 0; pc < fn.code.size(); ++pc) {
        if (local > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error("NQR-B9001", {}, "NQRI instruction index overflow");
            return false;
        }
        pcMap[pc] = static_cast<std::uint32_t>(local);
        local += expansionCount(fn.code[pc]);
    }
    if (local > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.error("NQR-B9001", {}, "NQRI function instruction count overflow");
        return false;
    }
    pcMap[fn.code.size()] = static_cast<std::uint32_t>(local);

    appendParameterPrefix(fn, out);
    std::int64_t nextTemp = static_cast<std::int64_t>(fn.nextReg + fn.params.size());

    auto absoluteTarget = [&](std::size_t target) -> std::optional<std::int64_t> {
        if (target >= pcMap.size()) return std::nullopt;
        const std::uint64_t absolute = static_cast<std::uint64_t>(globalFirst) + pcMap[target];
        if (absolute > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
        return static_cast<std::int64_t>(absolute);
    };

    for (const auto& i : fn.code) {
        StableInstruction x;
        x.dest = i.dest ? static_cast<std::int64_t>(*i.dest) : -1;
        x.flags = i.isVolatile ? kFlagVolatile : 0;
        x.width = i.width;

        switch (i.op) {
            case NirOp::Const:
                if (auto v = std::get_if<std::int64_t>(&i.literal)) {
                    x.opcode = 1; x.typeCode = 3; x.immediate = *v;
                } else if (auto v = std::get_if<double>(&i.literal)) {
                    x.opcode = 2; x.typeCode = 4; x.floatImmediate = *v;
                } else if (auto v = std::get_if<bool>(&i.literal)) {
                    x.opcode = 1; x.typeCode = 2; x.immediate = *v ? 1 : 0;
                } else if (auto v = std::get_if<std::string>(&i.literal)) {
                    x.opcode = 45; x.typeCode = 5;
                    const auto span = source.append(*v);
                    x.sourceStart = span.start; x.sourceEnd = span.end;
                } else {
                    x.opcode = 1; x.immediate = 0;
                }
                out.push_back(x);
                break;

            case NirOp::Load:
                x.opcode = 4; x.symbol = symbolId(i.text); out.push_back(x); break;
            case NirOp::Store:
                if (i.args.size() != 1) { diagnostics.error("NQR-B9002", {}, "malformed store in C++ NIR"); return false; }
                x.opcode = 5; x.left = i.args[0]; x.symbol = symbolId(i.text); out.push_back(x); break;
            case NirOp::Unary:
                if (i.args.size() != 1 || (i.text != "-" && i.text != "!")) { diagnostics.error("NQR-B9003", {}, "unsupported unary operation in NQRI bootstrap emitter"); return false; }
                x.opcode = i.text == "-" ? 11 : 12; x.left = i.args[0]; out.push_back(x); break;
            case NirOp::Binary: {
                if (i.args.size() != 2) { diagnostics.error("NQR-B9004", {}, "malformed binary operation in C++ NIR"); return false; }
                const auto opcode = binaryOpcode(i.text);
                if (opcode == std::numeric_limits<std::uint32_t>::max()) { diagnostics.error("NQR-B9005", {}, "unsupported binary operation in NQRI bootstrap emitter: " + i.text); return false; }
                x.opcode = opcode; x.left = i.args[0]; x.right = i.args[1]; out.push_back(x); break;
            }
            case NirOp::Cast:
                if (i.args.size() != 1) { diagnostics.error("NQR-B9006", {}, "malformed cast in C++ NIR"); return false; }
                x.opcode = 43; x.left = i.args[0]; x.typeCode = typeCode(i.text); out.push_back(x); break;
            case NirOp::Call: {
                for (std::size_t a = 0; a < i.args.size(); ++a) {
                    StableInstruction arg;
                    arg.opcode = 46;
                    arg.left = i.args[a];
                    arg.immediate = static_cast<std::int64_t>(a);
                    out.push_back(arg);
                }
                x.opcode = 21;
                x.symbol = symbolId(i.text);
                x.width = i.args.size();
                const auto span = source.append(i.text);
                x.sourceStart = span.start; x.sourceEnd = span.end;
                out.push_back(x);
                break;
            }
            case NirOp::AddressOf:
                x.opcode = 24; x.symbol = symbolId(i.text); out.push_back(x); break;
            case NirOp::LoadMemory:
                if (i.args.size() != 1) { diagnostics.error("NQR-B9007", {}, "malformed memory load in C++ NIR"); return false; }
                x.opcode = 38; x.left = i.args[0]; out.push_back(x); break;
            case NirOp::StoreMemory:
                if (i.args.size() != 2) { diagnostics.error("NQR-B9008", {}, "malformed memory store in C++ NIR"); return false; }
                x.opcode = 39; x.left = i.args[0]; x.right = i.args[1]; out.push_back(x); break;
            case NirOp::PtrOffset:
                if (i.args.empty() || i.args.size() > 2) { diagnostics.error("NQR-B9009", {}, "malformed pointer offset in C++ NIR"); return false; }
                if (i.args.size() == 1) {
                    StableInstruction zero;
                    zero.opcode = 1; zero.dest = nextTemp++; zero.immediate = 0; zero.typeCode = 3;
                    out.push_back(zero);
                    x.opcode = 25; x.left = i.args[0]; x.right = zero.dest; x.immediate = static_cast<std::int64_t>(i.target); x.width = 1;
                } else {
                    x.opcode = 25; x.left = i.args[0]; x.right = i.args[1]; x.immediate = 0;
                }
                out.push_back(x);
                break;
            case NirOp::StackAlloc:
                x.opcode = 37; out.push_back(x); break;
            case NirOp::MakeSlice:
                if (i.args.size() != 2) { diagnostics.error("NQR-B9010", {}, "malformed make-slice in C++ NIR"); return false; }
                x.opcode = 42; x.left = i.args[0]; x.right = i.args[1]; out.push_back(x); break;
            case NirOp::SliceData:
                if (i.args.size() != 1) { diagnostics.error("NQR-B9011", {}, "malformed slice-data in C++ NIR"); return false; }
                x.opcode = 40; x.left = i.args[0]; out.push_back(x); break;
            case NirOp::SliceLen:
                if (i.args.size() != 1) { diagnostics.error("NQR-B9012", {}, "malformed slice-len in C++ NIR"); return false; }
                x.opcode = 41; x.left = i.args[0]; out.push_back(x); break;
            case NirOp::CheckNonNull:
                // Portable loads/stores already trap null pointers. Keeping this
                // check implicit avoids adding a second portable opcode contract.
                break;
            case NirOp::CheckBounds:
                if (i.args.size() != 2) { diagnostics.error("NQR-B9013", {}, "malformed bounds check in C++ NIR"); return false; }
                x.opcode = 35; x.left = i.args[0]; x.right = i.args[1]; out.push_back(x); break;
            case NirOp::AtomicLoad:
                if (i.args.size()!=1) { diagnostics.error("NQR-B9029", {}, "malformed atomic load in C++ NIR"); return false; }
                x.opcode=26; x.left=i.args[0]; out.push_back(x); break;
            case NirOp::AtomicStore:
                if (i.args.size()!=2) { diagnostics.error("NQR-B9030", {}, "malformed atomic store in C++ NIR"); return false; }
                x.opcode=27; x.left=i.args[0]; x.right=i.args[1]; out.push_back(x); break;
            case NirOp::AtomicExchange:
                if (i.args.size()!=2) { diagnostics.error("NQR-B9031", {}, "malformed atomic exchange in C++ NIR"); return false; }
                x.opcode=28; x.left=i.args[0]; x.right=i.args[1]; out.push_back(x); break;
            case NirOp::AtomicCompareExchange:
                diagnostics.error("NQR-B9014", {}, "compare-exchange is not accepted in a trusted Stage-0 NQRI seed");
                return false;
            case NirOp::AtomicFence:
                x.opcode=30; out.push_back(x); break;
            case NirOp::Intrinsic:
                diagnostics.error("NQR-B9015", {}, "intrinsics are not accepted in a portable trusted NQRI seed");
                return false;
            case NirOp::InlineAsm:
                diagnostics.error("NQR-B9016", {}, "inline assembly is not accepted in a portable trusted NQRI seed");
                return false;
            case NirOp::Try:
                if (i.args.size()!=1) { diagnostics.error("NQR-B9032", {}, "malformed try instruction in C++ NIR"); return false; }
                x.opcode=33; x.left=i.args[0]; out.push_back(x); break;
            case NirOp::Throw:
                if (i.args.size()!=1) { diagnostics.error("NQR-B9033", {}, "malformed throw instruction in C++ NIR"); return false; }
                x.opcode=34; x.left=i.args[0]; out.push_back(x); break;
            case NirOp::Jump: {
                const auto target = absoluteTarget(i.target);
                if (!target) { diagnostics.error("NQR-B9017", {}, "jump target overflow in NQRI emitter"); return false; }
                x.opcode = 19; x.immediate = *target; out.push_back(x); break;
            }
            case NirOp::JumpIfFalse: {
                if (i.args.size()!=1) { diagnostics.error("NQR-B9034", {}, "malformed conditional branch in C++ NIR"); return false; }
                const auto target = absoluteTarget(i.target);
                if (!target) { diagnostics.error("NQR-B9018", {}, "branch target overflow in NQRI emitter"); return false; }
                x.opcode = 20; x.left = i.args[0]; x.immediate = *target; out.push_back(x); break;
            }
            case NirOp::Return:
                x.opcode = 22; if (!i.args.empty()) x.left = i.args[0]; out.push_back(x); break;
            case NirOp::Nop:
                x.opcode = 0; out.push_back(x); break;
        }
    }
    return true;
}

void writeU32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (std::size_t i=0;i<4;++i) out[offset+i]=static_cast<std::uint8_t>((value>>(i*8))&0xffu);
}
void writeU64(std::vector<std::uint8_t>& out, std::size_t offset, std::uint64_t value) {
    for (std::size_t i=0;i<8;++i) out[offset+i]=static_cast<std::uint8_t>((value>>(i*8))&0xffu);
}
void writeI64(std::vector<std::uint8_t>& out, std::size_t offset, std::int64_t value) {
    std::uint64_t raw=0; std::memcpy(&raw,&value,sizeof(raw)); writeU64(out,offset,raw);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw std::runtime_error("truncated NQRI u32");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t readU64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) throw std::runtime_error("truncated NQRI u64");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

bool validateSerializedNqri(const std::vector<std::uint8_t>& bytes, std::string& error) {
    if (bytes.size() < kHeaderBytes || bytes[0] != 'N' || bytes[1] != 'Q' || bytes[2] != 'R' || bytes[3] != 'I') {
        error = "invalid NQRI magic/header";
        return false;
    }
    if (readU32(bytes, 4) != kVersion || readU32(bytes, 20) != kFunctionBytes || readU32(bytes, 24) != kInstructionBytes) {
        error = "unsupported NQRI version or record size";
        return false;
    }
    const auto sourceBytes = readU32(bytes, 8);
    const auto functionCount = readU32(bytes, 12);
    const auto instructionCount = readU32(bytes, 16);
    const std::uint64_t expected = kHeaderBytes + static_cast<std::uint64_t>(sourceBytes) +
        static_cast<std::uint64_t>(functionCount) * kFunctionBytes +
        static_cast<std::uint64_t>(instructionCount) * kInstructionBytes;
    if (expected != bytes.size()) {
        error = "NQRI serialized length mismatch";
        return false;
    }
    std::size_t offset = kHeaderBytes + sourceBytes;
    std::unordered_set<std::uint64_t> symbols;
    for (std::uint32_t i = 0; i < functionCount; ++i, offset += kFunctionBytes) {
        const auto symbol = readU64(bytes, offset);
        const auto nameStart = readU32(bytes, offset + 8);
        const auto nameEnd = readU32(bytes, offset + 12);
        const auto firstInstruction = readU32(bytes, offset + 16);
        const auto count = readU32(bytes, offset + 20);
        if (!symbol || !symbols.insert(symbol).second || nameEnd <= nameStart || nameEnd > sourceBytes ||
            static_cast<std::uint64_t>(firstInstruction) + count > instructionCount) {
            error = "invalid NQRI function record";
            return false;
        }
    }
    for (std::uint32_t i = 0; i < instructionCount; ++i, offset += kInstructionBytes) {
        const auto opcode = readU32(bytes, offset);
        const auto sourceStart = readU64(bytes, offset + 64);
        const auto sourceEnd = readU64(bytes, offset + 72);
        if (opcode > 47 || sourceEnd < sourceStart || sourceEnd > sourceBytes) {
            error = "invalid NQRI instruction record";
            return false;
        }
    }
    return true;
}

} // namespace

bool NqriBackend::emitModule(const NirProgram& program,
                             const ModuleGraph* modules,
                             const std::filesystem::path& output,
                             Diagnostics& diagnostics) const {
    try {
        SourcePool source(modules);
        std::vector<StableInstruction> instructions;
        std::vector<StableFunction> functions;
        std::unordered_set<std::uint64_t> symbols;

        std::vector<const NirFunction*> ordered;
        ordered.push_back(&program.entry);
        for (const auto& fn : program.functions) if (!fn.isExtern) ordered.push_back(&fn);

        for (const auto* fn : ordered) {
            const auto symbol = symbolId(fn->name);
            if (!symbol || !symbols.insert(symbol).second) {
                diagnostics.error("NQR-B9020", {}, "duplicate/colliding function symbol in NQRI emitter: " + fn->name);
                return false;
            }
            if (instructions.size() > std::numeric_limits<std::uint32_t>::max()) {
                diagnostics.error("NQR-B9021", {}, "NQRI instruction table exceeds format limit");
                return false;
            }
            StableFunction record;
            record.symbol = symbol;
            record.name = source.append(fn->name);
            record.firstInstruction = static_cast<std::uint32_t>(instructions.size());
            if (fn->params.size() > std::numeric_limits<std::uint32_t>::max()) {
                diagnostics.error("NQR-B9040", {}, "NQRI parameter table exceeds 32-bit format limit: " + fn->name);
                return false;
            }
            record.parameterCount = static_cast<std::uint32_t>(fn->params.size());
            record.flags = fn->isExport ? 1u : 0u;
            const auto before = instructions.size();
            if (!translateFunction(*fn, record.firstInstruction, source, instructions, diagnostics)) return false;
            const auto count = instructions.size() - before;
            if (count > std::numeric_limits<std::uint32_t>::max()) {
                diagnostics.error("NQR-B9022", {}, "NQRI function exceeds instruction-count limit");
                return false;
            }
            record.instructionCount = static_cast<std::uint32_t>(count);
            functions.push_back(record);
        }

        if (source.bytes().size() > std::numeric_limits<std::uint32_t>::max() ||
            functions.size() > std::numeric_limits<std::uint32_t>::max() ||
            instructions.size() > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error("NQR-B9023", {}, "NQRI module exceeds 32-bit table limits");
            return false;
        }

        const std::uint64_t total64 = kHeaderBytes + source.bytes().size() +
            static_cast<std::uint64_t>(functions.size()) * kFunctionBytes +
            static_cast<std::uint64_t>(instructions.size()) * kInstructionBytes;
        if (total64 > std::numeric_limits<std::size_t>::max()) {
            diagnostics.error("NQR-B9024", {}, "NQRI module size exceeds host address range");
            return false;
        }

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(total64), 0);
        bytes[0]='N'; bytes[1]='Q'; bytes[2]='R'; bytes[3]='I';
        writeU32(bytes,4,kVersion);
        writeU32(bytes,8,static_cast<std::uint32_t>(source.bytes().size()));
        writeU32(bytes,12,static_cast<std::uint32_t>(functions.size()));
        writeU32(bytes,16,static_cast<std::uint32_t>(instructions.size()));
        writeU32(bytes,20,static_cast<std::uint32_t>(kFunctionBytes));
        writeU32(bytes,24,static_cast<std::uint32_t>(kInstructionBytes));
        writeU32(bytes,28,0);

        std::size_t offset=kHeaderBytes;
        std::copy(source.bytes().begin(),source.bytes().end(),bytes.begin()+static_cast<std::ptrdiff_t>(offset));
        offset+=source.bytes().size();

        for(const auto& fn:functions){
            writeU64(bytes,offset,fn.symbol);
            writeU32(bytes,offset+8,static_cast<std::uint32_t>(fn.name.start));
            writeU32(bytes,offset+12,static_cast<std::uint32_t>(fn.name.end));
            writeU32(bytes,offset+16,fn.firstInstruction);
            writeU32(bytes,offset+20,fn.instructionCount);
            writeU32(bytes,offset+24,fn.parameterCount);
            writeU32(bytes,offset+28,fn.flags);
            writeU64(bytes,offset+32,0);
            offset+=kFunctionBytes;
        }

        for(const auto& i:instructions){
            writeU32(bytes,offset,i.opcode);
            writeU32(bytes,offset+4,i.typeCode);
            writeU32(bytes,offset+8,i.flags);
            writeU32(bytes,offset+12,0);
            writeI64(bytes,offset+16,i.dest);
            writeI64(bytes,offset+24,i.left);
            writeI64(bytes,offset+32,i.right);
            writeI64(bytes,offset+40,i.immediate);
            writeU64(bytes,offset+48,i.symbol);
            writeU64(bytes,offset+56,i.width);
            writeU64(bytes,offset+64,i.sourceStart);
            writeU64(bytes,offset+72,i.sourceEnd);
            std::uint64_t floatBits=0; std::memcpy(&floatBits,&i.floatImmediate,sizeof(floatBits));
            writeU64(bytes,offset+80,floatBits);
            writeU64(bytes,offset+88,0);
            offset+=kInstructionBytes;
        }

        std::string validationError;
        if (!validateSerializedNqri(bytes, validationError)) {
            diagnostics.error("NQR-B9038", {}, "refusing to publish invalid NQRI output: " + validationError);
            return false;
        }

        const auto temporary = output.string() + ".tmp";
        {
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
            if(!file){diagnostics.error("NQR-B9025",{},"cannot write temporary NQRI output: "+temporary);return false;}
            file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            file.flush();
            if(!file){
                std::error_code removeError;
                std::filesystem::remove(temporary, removeError);
                diagnostics.error("NQR-B9026",{},"failed writing temporary NQRI output: "+temporary);
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::remove(output, ec);
        ec.clear();
        std::filesystem::rename(temporary, output, ec);
        if (ec) {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            diagnostics.error("NQR-B9039", {}, "failed publishing NQRI output atomically: " + ec.message());
            return false;
        }
        return true;
    } catch(const std::exception& error) {
        diagnostics.error("NQR-B9027",{},std::string("NQRI serialization failed: ")+error.what());
        return false;
    }
}

} // namespace noe
