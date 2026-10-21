#include "compiler.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
noe::NirInstruction constant(noe::Reg dest, std::int64_t value) {
    noe::NirInstruction i;
    i.op = noe::NirOp::Const;
    i.dest = dest;
    i.literal = value;
    return i;
}
noe::NirInstruction withArgs(noe::NirOp op, std::vector<noe::Reg> args) {
    noe::NirInstruction i;
    i.op = op;
    i.args = std::move(args);
    return i;
}
bool check(bool ok, const char* label) {
    if (!ok) std::cerr << "optimizer-liveness: " << label << " failed\n";
    return ok;
}
}

int main() {
    {
        // The unary result is unused, therefore its operand is also dead.
        // Previous DCE retained the operand because it counted all reads.
        noe::NirProgram p;
        auto dead = withArgs(noe::NirOp::Unary, {1});
        dead.dest = 2;
        dead.text = "-";
        p.entry.code = {constant(1, 42), dead, constant(3, 7), withArgs(noe::NirOp::Return, {3})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O1);
        if (!check(p.entry.code.size() == 2, "transitive dead chain")) return 1;
        if (!check(p.entry.code[0].dest == 3 && p.entry.code[1].op == noe::NirOp::Return, "live return preserved")) return 1;
    }
    {
        // Side effects and their operand dependency chain must be preserved.
        noe::NirProgram p;
        auto unary = withArgs(noe::NirOp::Unary, {1});
        unary.dest = 2;
        unary.text = "-";
        auto call = withArgs(noe::NirOp::Call, {2});
        call.dest = 3;
        p.entry.code = {constant(1, 42), unary, call, withArgs(noe::NirOp::Return, {})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O1);
        if (!check(p.entry.code.size() == 4, "side-effect dependencies retained")) return 1;
    }
    {
        // Compaction must retarget forward control-flow edges.
        noe::NirProgram p;
        auto branch = withArgs(noe::NirOp::JumpIfFalse, {1});
        branch.target = 5;
        auto dead = withArgs(noe::NirOp::Unary, {1});
        dead.dest = 2;
        p.entry.code = {withArgs(noe::NirOp::Nop, {}), constant(1, 0), branch,
                        dead, withArgs(noe::NirOp::Nop, {}), withArgs(noe::NirOp::Return, {1})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O1);
        if (!check(p.entry.code.size() == 3, "branch compaction")) return 1;
        if (!check(p.entry.code[1].op == noe::NirOp::JumpIfFalse && p.entry.code[1].target == 2, "jump retarget")) return 1;
    }
    return 0;
}
