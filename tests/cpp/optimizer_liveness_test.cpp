#include "compiler.hpp"
#include "optimizer_cost.hpp"
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
        // Candidate reporting counts each lexical loop containing a literal.
        // A nested backedge may cover the same constant twice; outside values
        // must not be counted. These are diagnostics, not LICM proofs.
        noe::NirFunction f;
        auto inner=withArgs(noe::NirOp::Jump,{});inner.target=2;
        auto outer=withArgs(noe::NirOp::Jump,{});outer.target=1;
        f.code={constant(0,99),constant(1,1),constant(2,2),
                constant(3,3),inner,constant(4,4),outer,
                constant(5,5),withArgs(noe::NirOp::Return,{})};
        const auto facts=noe::inspectOptimizations(f);
        if(!check(facts.candidateLoopConstants==6,
                  "linear-time loop candidate reporting preserves nested counts"))
            return 1;
    }
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
        // Integer local GVN: second identical addition can reuse the first
        // without rewriting uses outside its straight-line basic block.
        noe::NirProgram p;
        auto a=withArgs(noe::NirOp::Load,{});a.dest=1;a.text="x";
        auto b=withArgs(noe::NirOp::Load,{});b.dest=2;b.text="y";
        auto sum=withArgs(noe::NirOp::Binary,{1,2});sum.dest=3;sum.text="+";
        sum.type.kind=noe::TypeKind::Int;
        auto duplicate=sum;duplicate.dest=4;
        p.entry.code={a,b,sum,duplicate,withArgs(noe::NirOp::Return,{4})};
        const auto before=noe::inspectOptimizations(p.entry);
        if(!check(before.uniqueVirtualRegisterDefinitions && before.allDefinitionsDominateUses,
                  "SSA eligibility for GVN"))return 1;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        int additions=0;noe::Reg returned=0;
        for(const auto& in:p.entry.code){
            if(in.op==noe::NirOp::Binary&&in.text=="+")++additions;
            if(in.op==noe::NirOp::Return&&!in.args.empty())returned=in.args[0];
        }
        if(!check(additions==1 && returned==3,"local integer GVN"))return 1;
    }
    {
        // Block boundary separates the definition from the branch: SCCP may
        // propagate the proven boolean only when the definition dominates.
        noe::NirProgram p;
        auto flag=constant(1,0);flag.literal=true;
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=3;
        auto cond=withArgs(noe::NirOp::JumpIfFalse,{1});cond.target=6;
        p.entry.code={flag,jump,withArgs(noe::NirOp::Nop,{}),cond,
                      constant(2,42),withArgs(noe::NirOp::Return,{2}),
                      constant(3,999),withArgs(noe::NirOp::Return,{3})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool hasBad=false;int returned=0;
        for(const auto& in:p.entry.code){
            if(in.op==noe::NirOp::Const && std::holds_alternative<std::int64_t>(in.literal)
               &&std::get<std::int64_t>(in.literal)==999)hasBad=true;
            if(in.op==noe::NirOp::Return)++returned;
        }
        if(!check(!hasBad&&returned==1,"SSA SCCP across blocks"))return 1;
    }
    {
        // Repeated register definitions are not in SSA form: advanced passes
        // must not treat them as a single constant definition.
        noe::NirProgram p;
        p.entry.code={constant(1,8),constant(1,9),withArgs(noe::NirOp::Return,{1})};
        const auto facts=noe::inspectOptimizations(p.entry);
        if(!check(!facts.uniqueVirtualRegisterDefinitions,
                  "reject non-SSA reused virtual registers"))return 1;
    }
    {
        // x*0 and x%1 are integer identities; the previously computed x
        // stays intact until proven dead by the usual liveness pass.
        noe::NirProgram p;
        auto x=withArgs(noe::NirOp::Load,{});x.dest=1;x.text="x";
        auto product=withArgs(noe::NirOp::Binary,{1,2});product.dest=3;
        product.type.kind=noe::TypeKind::I64;product.text="*";
        p.entry.code={x,constant(2,0),product,withArgs(noe::NirOp::Return,{3})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool zero=false;
        for(const auto& in:p.entry.code)if(in.dest&&*in.dest==3&&in.op==noe::NirOp::Const)
          zero=std::get<std::int64_t>(in.literal)==0;
        if(!check(zero,"safe integer strength reduction"))return 1;
    }
    {
        // Minimal structured loop: loop-invariant literal executes in the
        // preheader, while the backedge skips that literal on subsequent trips.
        noe::NirProgram p;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="go";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{1});branch.target=7;
        auto val=withArgs(noe::NirOp::Load,{});val.dest=3;val.text="x";
        auto sum=withArgs(noe::NirOp::Binary,{2,3});sum.dest=4;sum.text="+";
        sum.type.kind=noe::TypeKind::Int;
        auto store=withArgs(noe::NirOp::Store,{4});store.text="out";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=1;
        p.entry.code={cond,branch,constant(2,5),val,sum,store,jump,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t literal=999,conditional=999,backedge=999;
        for(std::size_t i=0;i<p.entry.code.size();++i){
            const auto& in=p.entry.code[i];
            if(in.op==noe::NirOp::Const &&in.dest&&*in.dest==2)literal=i;
            if(in.op==noe::NirOp::JumpIfFalse)conditional=i;
            if(in.op==noe::NirOp::Jump)backedge=i;
        }
        if(!check(literal<conditional&&p.entry.code[backedge].target==conditional,
                  "structured LICM and backedge retarget"))return 1;
    }
    {
        noe::NirFunction f;
        auto memory=withArgs(noe::NirOp::StackAlloc,{});memory.dest=7;
        auto ptrRead=withArgs(noe::NirOp::LoadMemory,{7});ptrRead.dest=8;
        f.code={memory,ptrRead,withArgs(noe::NirOp::Return,{8})};
        const auto facts=noe::inspectOptimizations(f);
        if(!check(facts.nonEscapingStackAllocations==1,
                  "direct nonescaping stack allocation candidate"))return 1;
        f.code[2]=withArgs(noe::NirOp::Return,{7});
        if(!check(noe::inspectOptimizations(f).nonEscapingStackAllocations==0,
                  "returned address escapes"))return 1;
        const auto x86=noe::TargetCostModel::forTarget("x86_64-unknown-linux-gnu");
        const auto arm=noe::TargetCostModel::forTarget("aarch64-unknown-linux-gnu");
        const auto generic=noe::TargetCostModel::forTarget("unsupported-triple");
        if(!check(x86.supportsSimd()&&arm.supportsSimd()&&!generic.supportsSimd(),
                  "target cost model refuses unknown SIMD targets"))return 1;
    }
    {
        // Targeted O3 instruction scheduling: a literal may be sunk toward
        // its first use without changing Store order or jump destinations.
        noe::NirProgram p;
        auto a=withArgs(noe::NirOp::Load,{});a.dest=2;a.text="x";
        auto b=withArgs(noe::NirOp::Load,{});b.dest=3;b.text="y";
        auto store=withArgs(noe::NirOp::Store,{2,3});store.text="observe";
        auto plus=withArgs(noe::NirOp::Binary,{1,2});plus.dest=4;plus.text="+";
        plus.type.kind=noe::TypeKind::Int;
        p.entry.code={constant(1,7),a,b,store,plus,withArgs(noe::NirOp::Return,{4})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t literal=999,load=999,addition=999,write=999;
        for(std::size_t pc=0;pc<p.entry.code.size();++pc){
            const auto& v=p.entry.code[pc];
            if(v.op==noe::NirOp::Const&&v.dest&&*v.dest==1)literal=pc;
            if(v.op==noe::NirOp::Load&&v.dest&&*v.dest==2)load=pc;
            if(v.op==noe::NirOp::Binary&&v.dest&&*v.dest==4)addition=pc;
            if(v.op==noe::NirOp::Store)write=pc;
        }
        if(!check(load<write&&write<literal&&literal<addition,
                  "O3 local constant scheduling without effect movement"))return 1;
    }
    {
        noe::NirFunction f;
        auto ptrA=withArgs(noe::NirOp::StackAlloc,{});ptrA.dest=10;
        auto ptrB=withArgs(noe::NirOp::StackAlloc,{});ptrB.dest=11;
        auto derived=withArgs(noe::NirOp::PtrOffset,{10});derived.dest=12;
        f.code={ptrA,ptrB,derived,withArgs(noe::NirOp::LoadMemory,{12}),
            withArgs(noe::NirOp::Return,{})};
        noe::NirAliasAnalysis analysis(f);
        if(!check(analysis.query(10,11)==noe::AliasRelation::NoAlias,
                  "independent nonescaping allocas do not alias"))return 1;
        if(!check(analysis.query(10,12)==noe::AliasRelation::MayAlias &&
                  analysis.query(10,10)==noe::AliasRelation::MustAlias,
                  "same-root offset is conservatively MayAlias"))return 1;
        if(!check(!analysis.escapes(12),"derived local pointer does not escape"))return 1;
        f.code.back()=withArgs(noe::NirOp::Return,{12});
        noe::NirAliasAnalysis escaping(f);
        if(!check(escaping.escapes(10) &&
                  escaping.query(10,11)==noe::AliasRelation::MayAlias,
                  "escaping stack pointer suppresses NoAlias assumption"))return 1;
    }
    {
        // A register rewritten by a second definition is not a stable pointer
        // identity across the function, so MustAlias must not be asserted.
        noe::NirFunction f;
        auto a=withArgs(noe::NirOp::StackAlloc,{});a.dest=1;
        auto b=withArgs(noe::NirOp::StackAlloc,{});b.dest=1;
        f.code={a,b,withArgs(noe::NirOp::Return,{1})};
        if(!check(noe::NirAliasAnalysis(f).query(1,1)==noe::AliasRelation::MayAlias,
                  "non-SSA aliases are not proven identical"))return 1;
    }
    {
        // Floating-point common subexpressions cannot be folded as integers.
        // Signed zero and NaN behavior is not part of the current GVN proof.
        noe::NirProgram p;
        auto a=withArgs(noe::NirOp::Load,{});a.dest=1;
        auto b=withArgs(noe::NirOp::Load,{});b.dest=2;
        auto sum=withArgs(noe::NirOp::Binary,{1,2});
        sum.dest=3;sum.type.kind=noe::TypeKind::Float;sum.text="+";
        auto duplicate=sum;duplicate.dest=4;
        p.entry.code={a,b,sum,withArgs(noe::NirOp::Store,{3}),
                      duplicate,withArgs(noe::NirOp::Return,{4})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        int additions=0;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Binary&&in.text=="+")++additions;
        if(!check(additions==2,"floating GVN stays disabled"))return 1;
    }
    return 0;
}
