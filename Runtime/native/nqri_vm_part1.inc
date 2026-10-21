#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace noqeri::nqri {

struct RuntimeError : std::runtime_error {
    explicit RuntimeError(const std::string& message) : std::runtime_error(message) {}
};
struct ThrowSignal {
    std::int64_t value = 0;
};

struct MemoryBlock;
struct Cell;
struct Pointer;
struct Slice;
using PointerPtr = std::shared_ptr<Pointer>;
using SlicePtr = std::shared_ptr<Slice>;
using Value = std::variant<std::monostate, std::int64_t, double, PointerPtr, SlicePtr>;

struct MemoryBlock {
    explicit MemoryBlock(std::size_t bytes = 0, std::optional<std::size_t> logical = std::nullopt)
        : buffer(bytes, 0), logicalLength(logical) {}
    std::vector<std::uint8_t> buffer;
    std::unordered_map<std::size_t, Value> refs;
    std::optional<std::size_t> logicalLength;
    void ensure(std::size_t end) {
        if (end <= buffer.size()) return;
        std::size_t size = std::max<std::size_t>(16, buffer.size());
        while (size < end) {
            if (size > std::numeric_limits<std::size_t>::max() / 2) { size = end; break; }
            size = std::max(end, size * 2);
        }
        buffer.resize(size, 0);
    }
};
struct Cell {
    Value value = std::int64_t{0};
    std::shared_ptr<MemoryBlock> block;
};
struct Pointer {
    enum class Kind { Block, Cell } kind = Kind::Block;
    std::shared_ptr<MemoryBlock> block;
    std::shared_ptr<Cell> cell;
    std::int64_t offset = 0;
};
struct Slice {
    PointerPtr data;
    std::size_t length = 0;
};

static PointerPtr blockPtr(const std::shared_ptr<MemoryBlock>& block, std::int64_t offset = 0) {
    auto p = std::make_shared<Pointer>(); p->kind = Pointer::Kind::Block; p->block = block; p->offset = offset; return p;
}
static PointerPtr cellPtr(const std::shared_ptr<Cell>& cell, std::int64_t offset = 0) {
    auto p = std::make_shared<Pointer>(); p->kind = Pointer::Kind::Cell; p->cell = cell; p->offset = offset; return p;
}
static SlicePtr sliceValue(PointerPtr data, std::size_t length) {
    auto s = std::make_shared<Slice>(); s->data = std::move(data); s->length = length; return s;
}

static bool truthy(const Value& value) {
    if (const auto* p = std::get_if<std::int64_t>(&value)) return *p != 0;
    if (const auto* p = std::get_if<double>(&value)) return *p != 0.0 && !std::isnan(*p);
    if (const auto* p = std::get_if<PointerPtr>(&value)) return static_cast<bool>(*p);
    if (const auto* p = std::get_if<SlicePtr>(&value)) return static_cast<bool>(*p);
    return false;
}
static std::int64_t asInt(const Value& value) {
    if (const auto* p = std::get_if<std::int64_t>(&value)) return *p;
    if (const auto* p = std::get_if<double>(&value)) {
        if (!std::isfinite(*p)) throw RuntimeError("expected finite integer-compatible value");
        return static_cast<std::int64_t>(*p);
    }
    if (std::holds_alternative<std::monostate>(value)) return 0;
    throw RuntimeError("expected integer value");
}
static double asDouble(const Value& value) {
    if (const auto* p = std::get_if<double>(&value)) return *p;
    if (const auto* p = std::get_if<std::int64_t>(&value)) return static_cast<double>(*p);
    throw RuntimeError("expected numeric value");
}
static std::size_t checkedSize(std::int64_t value, const char* what) {
    if (value < 0) throw RuntimeError(std::string(what) + " cannot be negative");
    const auto u = static_cast<std::uint64_t>(value);
    if (u > std::numeric_limits<std::size_t>::max()) throw RuntimeError(std::string(what) + " exceeds host range");
    return static_cast<std::size_t>(u);
}
static std::int64_t checkedAdd(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) throw RuntimeError("checked i64 overflow");
    return a + b;
}
static std::int64_t checkedSub(std::int64_t a, std::int64_t b) {
    if ((b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)) throw RuntimeError("checked i64 overflow");
    return a - b;
}
static std::int64_t checkedMul(std::int64_t a, std::int64_t b) {
    if (a == 0 || b == 0) return 0;
    if ((a == -1 && b == std::numeric_limits<std::int64_t>::min()) ||
        (b == -1 && a == std::numeric_limits<std::int64_t>::min())) throw RuntimeError("checked i64 overflow");
    if (a > 0) {
        if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) ||
            (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a)) throw RuntimeError("checked i64 overflow");
    } else {
        if ((b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) ||
            (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b)) throw RuntimeError("checked i64 overflow");
    }
    return a * b;
}
static std::int64_t wrapAdd(std::int64_t a, std::int64_t b) {
    const auto u = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b);
    std::int64_t out; std::memcpy(&out, &u, sizeof(out)); return out;
}
static std::int64_t wrapSub(std::int64_t a, std::int64_t b) {
    const auto u = static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b);
    std::int64_t out; std::memcpy(&out, &u, sizeof(out)); return out;
}
static std::int64_t wrapMul(std::int64_t a, std::int64_t b) {
    const auto u = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    std::int64_t out; std::memcpy(&out, &u, sizeof(out)); return out;
}
static std::size_t widthForType(std::uint32_t typeCode) {
    if (typeCode == 2 || typeCode == 6 || typeCode == 7) return 1;
    if (typeCode == 8 || typeCode == 9) return 2;
    if (typeCode == 10 || typeCode == 11) return 4;
    return 8;
}
static bool signedType(std::uint32_t typeCode) {
    return typeCode == 3 || typeCode == 6 || typeCode == 8 || typeCode == 10 || typeCode == 12 || typeCode == 14;
}

struct ResolvedPointer { std::shared_ptr<MemoryBlock> block; std::size_t offset = 0; std::shared_ptr<Cell> cell; };
static ResolvedPointer resolvePointer(const PointerPtr& pointer) {
    if (!pointer) throw RuntimeError("null or non-pointer memory access");
    if (pointer->offset < 0) throw RuntimeError("negative pointer offset");
    if (pointer->kind == Pointer::Kind::Block) {
        if (!pointer->block) throw RuntimeError("null memory block");
        return {pointer->block, static_cast<std::size_t>(pointer->offset), {}};
    }
    if (!pointer->cell) throw RuntimeError("null cell pointer");
    if (pointer->offset == 0 && !std::holds_alternative<PointerPtr>(pointer->cell->value)) return {{}, 0, pointer->cell};
    if (const auto* nested = std::get_if<PointerPtr>(&pointer->cell->value); nested && *nested) {
        auto r = resolvePointer(*nested);
        const auto extra = static_cast<std::size_t>(pointer->offset);
        if (r.offset > std::numeric_limits<std::size_t>::max() - extra) throw RuntimeError("pointer offset overflow");
        r.offset += extra; return r;
    }
    if (!pointer->cell->block) pointer->cell->block = std::make_shared<MemoryBlock>(std::max<std::size_t>(16, static_cast<std::size_t>(pointer->offset) + 8));
    return {pointer->cell->block, static_cast<std::size_t>(pointer->offset), {}};
}
static void writeScalar(const PointerPtr& pointer, const Value& value, std::size_t width = 8) {
    auto target = resolvePointer(pointer);
    if (target.cell && target.offset == 0) { target.cell->value = value; return; }
    if (!target.block) throw RuntimeError("invalid write address");
    if (target.offset > std::numeric_limits<std::size_t>::max() - width) throw RuntimeError("write address overflow");
    target.block->ensure(target.offset + width);
    if (std::holds_alternative<PointerPtr>(value) || std::holds_alternative<SlicePtr>(value)) {
        target.block->refs[target.offset] = value;
        std::fill(target.block->buffer.begin() + static_cast<std::ptrdiff_t>(target.offset), target.block->buffer.begin() + static_cast<std::ptrdiff_t>(target.offset + width), std::uint8_t{0});
        return;
    }
    target.block->refs.erase(target.offset);
    // Preserve IEEE-754 payloads exactly. Integer conversion silently
    // truncated floating values, breaking Noqeri's pointer bitcasts and
    // making Stage-2/Stage-3 self-rebuilds differ by float-immediate bytes.
    std::uint64_t raw = 0;
    if (const auto* floating = std::get_if<double>(&value)) {
        if (width != sizeof(double)) throw RuntimeError("invalid f64 storage width");
        std::memcpy(&raw, floating, sizeof(raw));
    } else {
        raw = static_cast<std::uint64_t>(asInt(value));
    }
    if (width > sizeof(raw)) throw RuntimeError("invalid scalar storage width");
    for (std::size_t i = 0; i < width; ++i) target.block->buffer[target.offset + i] = static_cast<std::uint8_t>((raw >> (i * 8)) & 0xffu);
}
static Value readScalar(const PointerPtr& pointer, std::size_t width = 8, std::uint32_t typeCode = 0) {
    auto target = resolvePointer(pointer);
    if (target.cell && target.offset == 0) {
        // A typed pointer cast must view the original storage bits, not
        // numerically convert a float to an integer or vice versa.
        if (width == sizeof(double) && typeCode != 0) {
            if (auto floating = std::get_if<double>(&target.cell->value)) {
                if (typeCode != 4) {
                    std::uint64_t raw = 0; std::memcpy(&raw, floating, sizeof(raw));
                    std::int64_t bits = 0; std::memcpy(&bits, &raw, sizeof(bits)); return bits;
                }
            } else if (auto integer = std::get_if<std::int64_t>(&target.cell->value)) {
                if (typeCode == 4) {
                    double number = 0; std::memcpy(&number, integer, sizeof(number)); return number;
                }
            }
        }
        return target.cell->value;
    }
    if (!target.block || target.offset > target.block->buffer.size() || width > target.block->buffer.size() - target.offset) throw RuntimeError("out-of-bounds memory read");
    auto ref = target.block->refs.find(target.offset); if (ref != target.block->refs.end()) return ref->second;
    std::uint64_t raw = 0;
    for (std::size_t i = 0; i < width; ++i) raw |= static_cast<std::uint64_t>(target.block->buffer[target.offset + i]) << (i * 8);
    if (typeCode == 4) {
        if (width != sizeof(double)) throw RuntimeError("invalid f64 load width");
        double number = 0; std::memcpy(&number, &raw, sizeof(number)); return number;
    }
    if (signedType(typeCode) && width < 8 && (raw & (std::uint64_t{1} << (width * 8 - 1)))) raw |= (~std::uint64_t{0}) << (width * 8);
    std::int64_t out; std::memcpy(&out, &raw, sizeof(out)); return out;
}
