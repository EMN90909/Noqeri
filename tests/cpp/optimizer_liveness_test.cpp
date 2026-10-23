#include "compiler.hpp"
#include "pgo.hpp"
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
            if(in.op==noe::NirOp::JumpIfFalse&&conditional==999)conditional=i;
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
    {
        // Unrolling duplicates a full iteration including its exit check,
        // so a side-effectful body is still executed exactly once per trip.
        noe::NirProgram p;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="keep";
        auto checkExit=withArgs(noe::NirOp::JumpIfFalse,{1});checkExit.target=5;
        auto output=withArgs(noe::NirOp::Load,{});output.dest=2;output.text="item";
        auto write=withArgs(noe::NirOp::Store,{2});write.text="observed";
        auto repeat=withArgs(noe::NirOp::Jump,{});repeat.target=0;
        p.entry.code={cond,checkExit,output,write,repeat,withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::vector<std::size_t> testsAt,storesAt;
        std::size_t loopAt=99,exitAt=99;
        for(std::size_t pc=0;pc<p.entry.code.size();++pc){
            const auto& op=p.entry.code[pc];
            if(op.op==noe::NirOp::JumpIfFalse)testsAt.push_back(pc);
            if(op.op==noe::NirOp::Store)storesAt.push_back(pc);
            if(op.op==noe::NirOp::Jump)loopAt=pc;
            if(op.op==noe::NirOp::Return)exitAt=pc;
        }
        if(!check(testsAt.size()==2&&storesAt.size()==2&&loopAt!=99&&
                  testsAt[0]<storesAt[0]&&storesAt[0]<testsAt[1]&&
                  testsAt[1]<storesAt[1]&&storesAt[1]<loopAt,
                  "2x loop unrolling duplicates condition and effect order"))return 1;
        if(!check(p.entry.code[testsAt[0]].target==exitAt&&
                  p.entry.code[testsAt[1]].target==exitAt&&
                  p.entry.code[loopAt].target==0,
                  "unrolled loop preserves both exit edges and backedge"))return 1;
    }
    {
        // Exceptional control flow is outside the unroller's proof model.
        noe::NirProgram p;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="flag";
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{1});exit.target=5;
        auto next=withArgs(noe::NirOp::Load,{});next.dest=2;next.text="next";
        auto repeat=withArgs(noe::NirOp::Jump,{});repeat.target=0;
        p.entry.code={cond,exit,withArgs(noe::NirOp::Try,{1}),next,repeat,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        int branches=0;
        for(const auto& item:p.entry.code)
            if(item.op==noe::NirOp::JumpIfFalse)++branches;
        if(!check(branches==1,"no loop unroll across exception edge"))return 1;
    }
    {
        // An outside jump into the middle of a loop forbids duplication.
        noe::NirProgram p;
        auto enter=withArgs(noe::NirOp::Jump,{});enter.target=4;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="flag";
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{1});exit.target=6;
        auto item=withArgs(noe::NirOp::Load,{});item.dest=2;item.text="item";
        auto write=withArgs(noe::NirOp::Store,{2});write.text="out";
        auto repeat=withArgs(noe::NirOp::Jump,{});repeat.target=1;
        p.entry.code={enter,cond,exit,item,write,repeat,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        int stores=0;
        for(const auto& op:p.entry.code)
            if(op.op==noe::NirOp::Store)++stores;
        if(!check(stores==1,"no loop unroll with external body entry"))return 1;
    }
    {
        // IPO evaluates a bounded pure integer expression in a zero-arg
        // function, replaces the call and preserves the original result.
        noe::NirFunction sum;
        sum.name="compile_time_sum";
        auto plus=withArgs(noe::NirOp::Binary,{1,2});
        plus.dest=3;plus.text="+";plus.type.kind=noe::TypeKind::Int;
        sum.code={constant(1,19),constant(2,23),plus,
                  withArgs(noe::NirOp::Return,{3})};
        noe::NirProgram p;
        p.functions.push_back(sum);
        auto call=withArgs(noe::NirOp::Call,{});call.dest=9;
        call.text="compile_time_sum";
        p.entry.code={call,withArgs(noe::NirOp::Return,{9})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        if(!check(p.entry.code.front().op==noe::NirOp::Const &&
                  std::get<std::int64_t>(p.entry.code.front().literal)==42,
                  "pure IPO integer expression evaluated"))return 1;
    }
    {
        // An ambiguous function name must not be inferred pure just because
        // one of its definitions returns a constant.
        noe::NirFunction pure;
        pure.name="overloaded";
        pure.code={constant(1,42),withArgs(noe::NirOp::Return,{1})};
        noe::NirFunction impure=pure;
        impure.code={withArgs(noe::NirOp::Call,{}),
                     withArgs(noe::NirOp::Return,{})};
        noe::NirProgram p;
        p.functions={pure,impure};
        auto call=withArgs(noe::NirOp::Call,{});call.dest=2;
        call.text="overloaded";
        p.entry.code={call,withArgs(noe::NirOp::Return,{2})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        if(!check(p.entry.code.front().op==noe::NirOp::Call,
                  "ambiguous IPO symbol is not folded"))return 1;
    }
    {
        // Inlining does not bypass an effectful provider, even if a matching
        // pure zero-argument function exists elsewhere in the call graph.
        noe::NirFunction side;
        side.name="side_effect";
        auto store=withArgs(noe::NirOp::Store,{1});store.text="audit";
        side.code={constant(1,5),store,withArgs(noe::NirOp::Return,{1})};
        noe::NirProgram p;p.functions={side};
        auto call=withArgs(noe::NirOp::Call,{});call.dest=3;
        call.text="side_effect";
        p.entry.code={call,withArgs(noe::NirOp::Return,{3})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        if(!check(p.entry.code.front().op==noe::NirOp::Call,
                  "effectful IPO remains dynamic"))return 1;
    }
    {
        // O3 local DAG scheduling prioritizes the longer multiplication path
        // but preserves data dependencies and final returned register.
        noe::NirProgram p;
        auto loadX=withArgs(noe::NirOp::Load,{});loadX.dest=1;loadX.text="x";
        auto loadY=withArgs(noe::NirOp::Load,{});loadY.dest=2;loadY.text="y";
        auto add=withArgs(noe::NirOp::Binary,{1,2});
        add.dest=3;add.text="+";add.type.kind=noe::TypeKind::Int;
        auto multiply=withArgs(noe::NirOp::Binary,{1,2});
        multiply.dest=4;multiply.text="*";multiply.type.kind=noe::TypeKind::Int;
        auto total=withArgs(noe::NirOp::Binary,{3,4});
        total.dest=5;total.text="+";total.type.kind=noe::TypeKind::Int;
        p.entry.code={loadX,loadY,add,multiply,total,
                      withArgs(noe::NirOp::Return,{5})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t firstMultiply=99,firstAdd=99,totalAt=99;
        for(std::size_t i=0;i<p.entry.code.size();++i){
            const auto& in=p.entry.code[i];
            if(in.dest&&*in.dest==3)firstAdd=i;
            if(in.dest&&*in.dest==4)firstMultiply=i;
            if(in.dest&&*in.dest==5)totalAt=i;
        }
        if(!check(firstMultiply<firstAdd&&firstAdd<totalAt &&
                  p.entry.code.back().args.front()==5,
                  "DAG scheduling respects integer dependencies"))return 1;
    }
    {
        // A Store is an observable barrier and may not be crossed by
        // the arithmetic DAG scheduler.
        noe::NirProgram p;
        auto x=withArgs(noe::NirOp::Load,{});x.dest=1;x.text="x";
        auto y=withArgs(noe::NirOp::Load,{});y.dest=2;y.text="y";
        auto plus=withArgs(noe::NirOp::Binary,{1,2});
        plus.dest=3;plus.text="+";plus.type.kind=noe::TypeKind::Int;
        auto effect=withArgs(noe::NirOp::Store,{3});effect.text="audit";
        auto product=withArgs(noe::NirOp::Binary,{1,2});
        product.dest=4;product.text="*";product.type.kind=noe::TypeKind::Int;
        p.entry.code={x,y,plus,effect,product,withArgs(noe::NirOp::Return,{4})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t sum=99,store=99,mul=99;
        for(std::size_t i=0;i<p.entry.code.size();++i){
            const auto& in=p.entry.code[i];
            if(in.dest&&*in.dest==3)sum=i;
            if(in.op==noe::NirOp::Store)store=i;
            if(in.dest&&*in.dest==4)mul=i;
        }
        if(!check(sum<store&&store<mul,
                  "DAG scheduling does not cross effectful Store"))return 1;
    }
    {
        // SLP pack planning recognizes independent integer operations only
        // on targets whose cost model permits a profitable 2-lane shape.
        noe::NirFunction fn;
        for(noe::Reg r=1;r<=4;++r) {
            auto load=withArgs(noe::NirOp::Load,{});
            load.dest=r;load.text="v"+std::to_string(r);
            fn.code.push_back(load);
        }
        auto mulA=withArgs(noe::NirOp::Binary,{1,2});
        mulA.dest=5;mulA.text="*";mulA.type.kind=noe::TypeKind::I64;
        auto mulB=withArgs(noe::NirOp::Binary,{3,4});
        mulB.dest=6;mulB.text="*";mulB.type.kind=noe::TypeKind::I64;
        fn.code.push_back(mulA);fn.code.push_back(mulB);
        fn.code.push_back(withArgs(noe::NirOp::Store,{5,6}));
        const auto x86=noe::TargetCostModel::forTarget("x86_64-unknown-linux-gnu");
        const auto unsupported=noe::TargetCostModel::forTarget("unknown");
        const auto packs=noe::inspectSlpPacks(fn,x86);
        if(!check(packs.size()==1&&packs[0].first==4&&
                  packs[0].second==5&&packs[0].laneBits==64,
                  "independent 2x i64 SIMD pack candidate"))return 1;
        if(!check(noe::inspectSlpPacks(fn,unsupported).empty(),
                  "SLP refuses unsupported target"))return 1;
        fn.code[5].args={5,4};
        if(!check(noe::inspectSlpPacks(fn,x86).empty(),
                  "SLP rejects cross-lane dependence"))return 1;
        fn.code[5].args={3,4};
        fn.code[4].type.kind=noe::TypeKind::Float;
        if(!check(noe::inspectSlpPacks(fn,x86).empty(),
                  "SLP rejects floating-point and type mismatch"))return 1;
    }
    {
        // A wrapping integer sum of two registers defined before the loop
        // is truly invariant. O3 may compute it before the zero-trip check.
        noe::NirProgram p;
        auto a=withArgs(noe::NirOp::Load,{});a.dest=1;a.text="left";
        auto b=withArgs(noe::NirOp::Load,{});b.dest=2;b.text="right";
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=3;flag.text="again";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=7;
        auto sum=withArgs(noe::NirOp::Binary,{1,2});
        sum.dest=4;sum.text="+";sum.type.kind=noe::TypeKind::I64;
        auto store=withArgs(noe::NirOp::Store,{4});store.text="audit";
        auto repeat=withArgs(noe::NirOp::Jump,{});repeat.target=2;
        p.entry.code={a,b,flag,branch,sum,store,repeat,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t arithmetic=99,condition=99;
        for(std::size_t i=0;i<p.entry.code.size();++i){
            const auto& in=p.entry.code[i];
            if(in.op==noe::NirOp::Binary&&in.dest&&*in.dest==4)arithmetic=i;
            if(in.op==noe::NirOp::JumpIfFalse&&condition==99)condition=i;
        }
        if(!check(arithmetic!=99&&arithmetic<condition,
                  "LICM hoists proven wrapping integer sum"))return 1;
    }
    {
        // NQPGO profiles are bound to the exact source-NIR revision. Only
        // hotness/cost decisions depend on counters, never legality rules.
        noe::NirProgram program;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="loop";
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{1});exit.target=5;
        auto value=withArgs(noe::NirOp::Load,{});value.dest=2;value.text="value";
        auto store=withArgs(noe::NirOp::Store,{2});store.text="audit";
        auto back=withArgs(noe::NirOp::Jump,{});back.target=0;
        program.entry.code={cond,exit,value,store,back,withArgs(noe::NirOp::Return,{})};
        noe::NirExecutionProfile cold;
        cold.begin(program);
        const auto text=cold.serialize();
        const auto loaded=noe::NirExecutionProfile::parse(text,program);
        if(!check(loaded.has_value()&&loaded->compatible(program),
                  "PGO versioned profile round-trip"))return 1;
        auto coldProgram=program;
        noe::PassManager(noe::OptimizationLevel::O3,&*loaded).optimize(coldProgram);
        int coldChecks=0;
        for(const auto& op:coldProgram.entry.code)
            if(op.op==noe::NirOp::JumpIfFalse)++coldChecks;
        if(!check(coldChecks==1,"cold PGO keeps compact loop"))return 1;
        noe::NirExecutionProfile hot;
        hot.begin(program);
        for(int n=0;n<80;++n)hot.record("@entry",0);
        auto optimized=program;
        noe::PassManager(noe::OptimizationLevel::O3,&hot).optimize(optimized);
        int hotChecks=0;
        for(const auto& op:optimized.entry.code)
            if(op.op==noe::NirOp::JumpIfFalse)++hotChecks;
        if(!check(hotChecks==2,"hot PGO selects semantics-preserving 2x unroll"))return 1;
        program.entry.code[2].text="changed_value";
        if(!check(!noe::NirExecutionProfile::parse(hot.serialize(),program).has_value(),
                  "reject stale PGO profile fingerprint"))return 1;
        bool rejected=false;
        try{noe::PassManager(noe::OptimizationLevel::O3,&hot).optimize(program);}
        catch(const std::invalid_argument&){rejected=true;}
        if(!check(rejected,"optimizer refuses mismatched PGO feedback"))return 1;
    }
    {
        // Target cost model is not decorative: conservative ARM64's lower
        // branch penalty avoids duplicating a small loop at O3, while the
        // x86-64 heuristic permits the same semantics-preserving transform.
        noe::NirProgram program;
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=1;cond.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{1});branch.target=5;
        auto val=withArgs(noe::NirOp::Load,{});val.dest=2;val.text="next";
        auto store=withArgs(noe::NirOp::Store,{2});store.text="out";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=0;
        program.entry.code={cond,branch,val,store,jump,
                            withArgs(noe::NirOp::Return,{})};
        auto generic=program,fast=program;
        noe::Optimizer{}.optimize(generic,noe::OptimizationLevel::O3,
                                 "aarch64-unknown-linux-gnu");
        noe::Optimizer{}.optimize(fast,noe::OptimizationLevel::O3,
                                 "x86_64-unknown-linux-gnu");
        int genericChecks=0,fastChecks=0;
        for(const auto& op:generic.entry.code)
            if(op.op==noe::NirOp::JumpIfFalse)++genericChecks;
        for(const auto& op:fast.entry.code)
            if(op.op==noe::NirOp::JumpIfFalse)++fastChecks;
        if(!check(genericChecks==1&&fastChecks==2,
                  "target-aware cost model controls O3 unroll"))return 1;
    }
    return 0;
}
