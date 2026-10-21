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
        if (!check(p.entry.code[0].dest == noe::Reg{3} && p.entry.code[1].op == noe::NirOp::Return, "live return preserved")) return 1;
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
    {
        // Unreachable straight-line tail must disappear, not just pure values.
        noe::NirProgram p;
        p.entry.code = {constant(1, 5), withArgs(noe::NirOp::Return, {1}),
                        constant(2, 9), withArgs(noe::NirOp::Return, {2})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        if (!check(p.entry.code.size() == 2, "unreachable tail elimination")) return 1;
        if (!check(p.entry.code[1].op == noe::NirOp::Return, "live return after pruning")) return 1;
    }
    {
        // A proven false boolean folds to an unconditional jump. The skipped
        // return and its operand must not survive the CFG reachability pass.
        noe::NirProgram p;
        auto b = constant(1, 0);
        b.literal = false;
        auto branch = withArgs(noe::NirOp::JumpIfFalse, {1});
        branch.target = 4;
        p.entry.code = {b, branch, constant(2, 999), withArgs(noe::NirOp::Return, {2}),
                        constant(3, 42), withArgs(noe::NirOp::Return, {3})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        int returns = 0;
        for (const auto& in : p.entry.code) {
            if (in.op == noe::NirOp::Return) ++returns;
            if (in.op == noe::NirOp::Const && std::holds_alternative<std::int64_t>(in.literal) &&
                std::get<std::int64_t>(in.literal) == 999) return 1;
        }
        if (!check(returns == 1, "branch folds and removes unreachable return")) return 1;
    }
    {
        // Exceptional control flow has implicit edges. Until modeled, reachability
        // must not prune lexical handlers after a return.
        noe::NirProgram p;
        p.entry.code = {withArgs(noe::NirOp::Try, {}), withArgs(noe::NirOp::Return, {}),
                        constant(1, 9), withArgs(noe::NirOp::Return, {1})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        int returns = 0;
        for (const auto& in : p.entry.code)
            if (in.op == noe::NirOp::Return) ++returns;
        if (!check(returns == 2, "conservative exception edges")) return 1;
    }
    {
        // The only inlined call shape is a pure, parameter-free constant return.
        noe::NirFunction literal;
        literal.name = "literal_answer";
        literal.code = {constant(1, 42), withArgs(noe::NirOp::Return, {1})};
        noe::NirProgram p;
        p.functions.push_back(literal);
        auto call = withArgs(noe::NirOp::Call, {});
        call.text = "literal_answer";
        call.dest = 5;
        p.entry.code = {call, withArgs(noe::NirOp::Return, {5})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        if (!check(p.entry.code.size() == 2 && p.entry.code[0].op == noe::NirOp::Const,
                   "pure constant call inlining")) return 1;
        if (!check(std::get<std::int64_t>(p.entry.code[0].literal) == 42,
                   "inlined literal preserved")) return 1;
    }
    {
        // Calls with effects, unknown names or extern providers are not inlined.
        noe::NirFunction external;
        external.name = "external_answer";
        external.isExtern = true;
        external.code = {constant(1, 42), withArgs(noe::NirOp::Return, {1})};
        noe::NirProgram p;
        p.functions.push_back(external);
        auto call = withArgs(noe::NirOp::Call, {});
        call.text = "external_answer";
        call.dest = 5;
        p.entry.code = {call, withArgs(noe::NirOp::Return, {5})};
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        if (!check(p.entry.code[0].op == noe::NirOp::Call, "external call kept")) return 1;
    }
    {
        // Local GVN replaces a redundant commutative integer expression and
        // rewires its use. It must never introduce an undefined register.
        noe::NirProgram p;
        auto add1 = withArgs(noe::NirOp::Binary, {1, 2});
        add1.text = "+"; add1.dest = 3; add1.type.kind = noe::TypeKind::Int;
        auto add2 = withArgs(noe::NirOp::Binary, {2, 1});
        add2.text = "+"; add2.dest = 4; add2.type.kind = noe::TypeKind::Int;
        p.entry.code = {constant(1, 8), constant(2, 13), add1, add2,
                        withArgs(noe::NirOp::Return, {4})};
        p.entry.registerTypes.resize(5);
        for (auto& type : p.entry.registerTypes) type.kind = noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        int additions = 0;
        for (const auto& instruction : p.entry.code)
            if (instruction.op == noe::NirOp::Binary) ++additions;
        if (!check(additions == 1, "GVN duplicate integer addition eliminated")) return 1;
        if (!check(p.entry.code.back().args.size() == 1 &&
                   p.entry.code.back().args[0] == 3,
                   "GVN uses original SSA result")) return 1;
    }
    {
        // SSA is not guaranteed for mutable/reused destination registers.
        // Fall back to unchanged binary operations until Phi construction.
        noe::NirProgram p;
        auto a = withArgs(noe::NirOp::Binary, {1, 2});
        a.text = "+"; a.dest = 3; a.type.kind = noe::TypeKind::Int;
        auto b = a; b.dest = 4;
        p.entry.code = {constant(1, 1), constant(2, 2), a,
                        constant(1, 8), b, withArgs(noe::NirOp::Return, {4})};
        p.entry.registerTypes.resize(5);
        for (auto& type : p.entry.registerTypes) type.kind = noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        int additions = 0;
        for (const auto& in : p.entry.code)
            if (in.op == noe::NirOp::Binary) ++additions;
        if (!check(additions == 2, "GVN rejects non-SSA register redefinition")) return 1;
    }
    {
        // Branch joins require Phi/dominance; local GVN skips the whole function.
        noe::NirProgram p;
        auto a = withArgs(noe::NirOp::Binary, {1, 2});
        a.text = "+"; a.dest = 3; a.type.kind = noe::TypeKind::Int;
        auto b = a; b.dest = 4;
        auto j = withArgs(noe::NirOp::JumpIfFalse, {5}); j.target = 5;
        p.entry.code = {constant(1, 8), constant(2, 13), a, j, b,
                        withArgs(noe::NirOp::Return, {4})};
        p.entry.registerTypes.resize(6);
        for (auto& type : p.entry.registerTypes) type.kind = noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p, noe::OptimizationLevel::O2);
        int additions = 0;
        for (const auto& in : p.entry.code)
            if (in.op == noe::NirOp::Binary) ++additions;
        if (!check(additions == 2, "GVN refuses CFG join without SSA")) return 1;
    }
    return 0;
}
