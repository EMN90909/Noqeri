#include "compiler.hpp"
#include "optimizer_cost.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
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
        // A 2048-element transitive chain is live when returned, even when
        // every link has a separate virtual register and the first input is
        // unknown. The old repeated global scans were quadratic here.
        noe::NirProgram p;
        auto input=withArgs(noe::NirOp::Load,{});input.dest=1;input.text="x";
        p.entry.code.push_back(input);
        for(noe::Reg r=2;r<=2049;++r){
            auto next=withArgs(noe::NirOp::Unary,{r-1});
            next.dest=r;next.text="-";
            p.entry.code.push_back(next);
        }
        p.entry.code.push_back(withArgs(noe::NirOp::Return,{2049}));
        p.entry.registerTypes.resize(2050);
        for(noe::Reg r=1;r<=2049;++r)
            p.entry.registerTypes[r].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O1);
        if(!check(p.entry.code.size()==2050,
                  "worklist preserves long transitive live unary chain"))
            return 1;
    }
    {
        // Entirely unused chains must still be deleted transitively.
        noe::NirProgram p;
        p.entry.code.push_back(constant(1,7));
        for(noe::Reg r=2;r<=512;++r){
            auto next=withArgs(noe::NirOp::Unary,{r-1});
            next.dest=r;next.text="-";
            p.entry.code.push_back(next);
        }
        p.entry.code.push_back(withArgs(noe::NirOp::Return,{}));
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O1);
        if(!check(p.entry.code.size()==1&&
                  p.entry.code[0].op==noe::NirOp::Return,
                  "worklist removes 512 dead transitive values"))
            return 1;
    }
    {
        // A long single-definition dependency chain must propagate without
        // changing wraparound integer semantics or leaving live binary ops.
        noe::NirProgram p;
        p.entry.code.push_back(constant(1,1));
        p.entry.code.push_back(constant(2,2));
        for(noe::Reg r=3;r<=1026;++r){
            auto next=withArgs(noe::NirOp::Binary,{r-1,2});
            next.dest=r;
            next.text="+";
            next.type.kind=noe::TypeKind::Int;
            p.entry.code.push_back(next);
        }
        p.entry.code.push_back(withArgs(noe::NirOp::Return,{1026}));
        p.entry.registerTypes.resize(1027);
        for(noe::Reg r=1;r<=1026;++r)
            p.entry.registerTypes[r].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool finalConstant=false;
        for(const auto& in:p.entry.code)
            if(in.dest&&*in.dest==1026&&in.op==noe::NirOp::Const)
                finalConstant=std::get<std::int64_t>(in.literal)==2050;
        if(!check(finalConstant,"worklist folds 1024 dependent SSA additions"))
            return 1;
    }
    {
        // The same scalar value reaches both arms of a diamond: a Load
        // after the join is redundant, and all successor uses may be
        // redirected to the source virtual register.
        noe::NirProgram p;
        auto store=withArgs(noe::NirOp::Store,{1});store.text="x";
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=2;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{2});branch.target=6;
        auto unused=withArgs(noe::NirOp::Load,{});unused.dest=3;unused.text="x";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=7;
        auto joined=withArgs(noe::NirOp::Load,{});joined.dest=4;joined.text="x";
        p.entry.code={constant(1,42),store,flag,branch,unused,jump,
                      withArgs(noe::NirOp::Nop,{}),joined,
                      withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        for(auto reg:{noe::Reg{1},noe::Reg{3},noe::Reg{4}})
            p.entry.registerTypes[reg].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Bool;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x"){
                if(!check(false,"diamond same-source load removed"))return 1;
            }
        bool returnedOriginal=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Return&&!in.args.empty()&&
               in.args[0]==1)returnedOriginal=true;
        if(!check(returnedOriginal,"diamond join uses dominating register"))
            return 1;
    }
    {
        // Incoming values differ. A real Phi would be necessary to
        // replace the final Load, so keep it until Phi lowering exists.
        noe::NirProgram p;
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=3;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=5;
        auto left=withArgs(noe::NirOp::Store,{1});left.text="x";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=7;
        auto right=withArgs(noe::NirOp::Store,{2});right.text="x";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=4;load.text="x";
        p.entry.code={constant(1,11),flag,branch,left,jump,
                      constant(2,22),right,load,
                      withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Int;
        p.entry.registerTypes[3].kind=noe::TypeKind::Bool;
        p.entry.registerTypes[4].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"diverging diamond values retain load"))return 1;
    }
    {
        // An assigned loop-carried value may differ on each iteration.
        // The exit Load must not be replaced by the preheader value.
        noe::NirProgram p;
        auto init=withArgs(noe::NirOp::Store,{1});init.text="x";
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=2;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{2});branch.target=7;
        auto body=withArgs(noe::NirOp::Store,{3});body.text="x";
        auto back=withArgs(noe::NirOp::Jump,{});back.target=2;
        auto after=withArgs(noe::NirOp::Load,{});after.dest=4;after.text="x";
        p.entry.code={constant(1,10),init,flag,branch,constant(3,99),
                      body,back,after,withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Bool;
        p.entry.registerTypes[3].kind=noe::TypeKind::Int;
        p.entry.registerTypes[4].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"loop-carried scalar keeps exit load"))return 1;
    }
    {
        // Address-taken and volatile locals are never promoted.
        noe::NirProgram p;
        auto init=withArgs(noe::NirOp::Store,{1});init.text="x";
        auto address=withArgs(noe::NirOp::AddressOf,{});address.dest=2;address.text="x";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=3;load.text="x";
        p.entry.code={constant(1,42),init,address,load,
                      withArgs(noe::NirOp::Return,{3})};
        p.entry.registerTypes.resize(4);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[3].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"address-taken named local stays in memory"))return 1;
    }
    {
        // CFG dominance must remain valid beyond one 64-bit bitset word.
        // A first-block definition dominates the final use across 96 jumps.
        noe::NirFunction fn;
        constexpr std::size_t blocks=96;
        for(std::size_t k=0;k<blocks;++k){
            fn.code.push_back(constant(static_cast<noe::Reg>(k+1),
                                       static_cast<std::int64_t>(k+1)));
            if(k+1<blocks){
                auto jump=withArgs(noe::NirOp::Jump,{});
                jump.target=2*(k+1);
                fn.code.push_back(jump);
            }else fn.code.push_back(withArgs(noe::NirOp::Return,{1}));
        }
        const auto facts=noe::inspectOptimizations(fn);
        if(!check(facts.basicBlocks==blocks&&
                  facts.uniqueVirtualRegisterDefinitions&&
                  facts.allDefinitionsDominateUses,
                  "packed dominators preserve 96-block SSA dominance"))
            return 1;
    }
    {
        // The same scalar value reaches both arms of a diamond: a Load
        // after the join is redundant, and all successor uses may be
        // redirected to the source virtual register.
        noe::NirProgram p;
        auto store=withArgs(noe::NirOp::Store,{1});store.text="x";
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=2;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{2});branch.target=6;
        auto unused=withArgs(noe::NirOp::Load,{});unused.dest=3;unused.text="x";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=7;
        auto joined=withArgs(noe::NirOp::Load,{});joined.dest=4;joined.text="x";
        p.entry.code={constant(1,42),store,flag,branch,unused,jump,
                      withArgs(noe::NirOp::Nop,{}),joined,
                      withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        for(auto reg:{noe::Reg{1},noe::Reg{3},noe::Reg{4}})
            p.entry.registerTypes[reg].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Bool;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x"){
                if(!check(false,"diamond same-source load removed"))return 1;
            }
        bool returnedOriginal=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Return&&!in.args.empty()&&
               in.args[0]==1)returnedOriginal=true;
        if(!check(returnedOriginal,"diamond join uses dominating register"))
            return 1;
    }
    {
        // Incoming values differ. A real Phi would be necessary to
        // replace the final Load, so keep it until Phi lowering exists.
        noe::NirProgram p;
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=3;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=5;
        auto left=withArgs(noe::NirOp::Store,{1});left.text="x";
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=7;
        auto right=withArgs(noe::NirOp::Store,{2});right.text="x";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=4;load.text="x";
        p.entry.code={constant(1,11),flag,branch,left,jump,
                      constant(2,22),right,load,
                      withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Int;
        p.entry.registerTypes[3].kind=noe::TypeKind::Bool;
        p.entry.registerTypes[4].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"diverging diamond values retain load"))return 1;
    }
    {
        // An assigned loop-carried value may differ on each iteration.
        // The exit Load must not be replaced by the preheader value.
        noe::NirProgram p;
        auto init=withArgs(noe::NirOp::Store,{1});init.text="x";
        auto flag=withArgs(noe::NirOp::Load,{});flag.dest=2;flag.text="flag";
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{2});branch.target=7;
        auto body=withArgs(noe::NirOp::Store,{3});body.text="x";
        auto back=withArgs(noe::NirOp::Jump,{});back.target=2;
        auto after=withArgs(noe::NirOp::Load,{});after.dest=4;after.text="x";
        p.entry.code={constant(1,10),init,flag,branch,constant(3,99),
                      body,back,after,withArgs(noe::NirOp::Return,{4})};
        p.entry.registerTypes.resize(5);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Bool;
        p.entry.registerTypes[3].kind=noe::TypeKind::Int;
        p.entry.registerTypes[4].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"loop-carried scalar keeps exit load"))return 1;
    }
    {
        // Address-taken and volatile locals are never promoted.
        noe::NirProgram p;
        auto init=withArgs(noe::NirOp::Store,{1});init.text="x";
        auto address=withArgs(noe::NirOp::AddressOf,{});address.dest=2;address.text="x";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=3;load.text="x";
        p.entry.code={constant(1,42),init,address,load,
                      withArgs(noe::NirOp::Return,{3})};
        p.entry.registerTypes.resize(4);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[3].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load&&in.text=="x")kept=true;
        if(!check(kept,"address-taken named local stays in memory"))return 1;
    }
    {
        // Two guarded true edges jointly prove a signed index in [0,8).
        // Unlike an unsigned upper guard alone, signed indices need a
        // separate lower-bound guarantee before CheckBounds may disappear.
        noe::NirProgram p;
        auto lowerIndex=withArgs(noe::NirOp::Load,{});lowerIndex.dest=1;lowerIndex.text="index";
        auto lower=withArgs(noe::NirOp::Binary,{1,2});
        lower.dest=3;lower.text=">=";lower.type.kind=noe::TypeKind::Bool;
        auto lowerExit=withArgs(noe::NirOp::JumpIfFalse,{3});lowerExit.target=12;
        auto upperIndex=withArgs(noe::NirOp::Load,{});upperIndex.dest=4;upperIndex.text="index";
        auto upper=withArgs(noe::NirOp::Binary,{4,5});
        upper.dest=6;upper.text="<";upper.type.kind=noe::TypeKind::Bool;
        auto upperExit=withArgs(noe::NirOp::JumpIfFalse,{6});upperExit.target=12;
        auto index=withArgs(noe::NirOp::Load,{});index.dest=7;index.text="index";
        auto bounds=withArgs(noe::NirOp::CheckBounds,{7,5});
        p.entry.code={lowerIndex,constant(2,0),lower,lowerExit,upperIndex,
                      constant(5,8),upper,upperExit,index,bounds,
                      constant(8,42),withArgs(noe::NirOp::Return,{8}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(9);
        for(noe::Reg r:{1u,4u,7u})p.entry.registerTypes[r].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool remaining=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)remaining=true;
        if(!check(!remaining,"signed index with two proven bounds"))return 1;
    }
    {
        // A second trip through the body without rechecking the upper guard
        // must not inherit the old bound proof. Mutation after the first
        // checked access makes the second access invalid.
        noe::NirProgram p;
        auto oldIndex=withArgs(noe::NirOp::Load,{});
        oldIndex.dest=1;oldIndex.text="index";
        auto guard=withArgs(noe::NirOp::Binary,{1,2});
        guard.dest=3;guard.text="<";guard.type.kind=noe::TypeKind::Bool;
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{3});
        exit.target=10;
        auto checked=withArgs(noe::NirOp::Load,{});
        checked.dest=4;checked.text="index";
        auto write=withArgs(noe::NirOp::Store,{5});write.text="index";
        auto back=withArgs(noe::NirOp::Jump,{});
        back.target=4;
        p.entry.code={oldIndex,constant(2,8),guard,exit,
                      checked,withArgs(noe::NirOp::CheckBounds,{4,2}),
                      constant(5,99),write,back,
                      withArgs(noe::NirOp::Nop,{}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(6);
        p.entry.registerTypes[1].kind=noe::TypeKind::Usize;
        p.entry.registerTypes[4].kind=noe::TypeKind::Usize;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool hasCheck=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)hasCheck=true;
        if(!check(hasCheck,"backedge cannot bypass bounds guard"))return 1;
    }
    {
        // An upper guard does NOT rule out negative indices. Keep the trap.
        noe::NirProgram p;
        auto index=withArgs(noe::NirOp::Load,{});index.dest=1;index.text="index";
        auto comparison=withArgs(noe::NirOp::Binary,{1,2});
        comparison.dest=3;comparison.text="<";comparison.type.kind=noe::TypeKind::Bool;
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=7;
        auto checked=withArgs(noe::NirOp::Load,{});checked.dest=4;checked.text="index";
        p.entry.code={index,constant(2,8),comparison,branch,checked,
                      withArgs(noe::NirOp::CheckBounds,{4,2}),
                      withArgs(noe::NirOp::Return,{}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(5);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[4].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)kept=true;
        if(!check(kept,"negative signed index must still trap"))return 1;
    }
    {
        // Reassigning the index after signed guards invalidates both facts.
        noe::NirProgram p;
        auto lowerIndex=withArgs(noe::NirOp::Load,{});lowerIndex.dest=1;lowerIndex.text="index";
        auto lower=withArgs(noe::NirOp::Binary,{1,2});
        lower.dest=3;lower.text=">=";lower.type.kind=noe::TypeKind::Bool;
        auto lowerExit=withArgs(noe::NirOp::JumpIfFalse,{3});lowerExit.target=13;
        auto upperIndex=withArgs(noe::NirOp::Load,{});upperIndex.dest=4;upperIndex.text="index";
        auto upper=withArgs(noe::NirOp::Binary,{4,5});
        upper.dest=6;upper.text="<";upper.type.kind=noe::TypeKind::Bool;
        auto upperExit=withArgs(noe::NirOp::JumpIfFalse,{6});upperExit.target=13;
        auto assignment=withArgs(noe::NirOp::Store,{8});assignment.text="index";
        auto current=withArgs(noe::NirOp::Load,{});current.dest=7;current.text="index";
        p.entry.code={lowerIndex,constant(2,0),lower,lowerExit,upperIndex,
                      constant(5,8),upper,upperExit,constant(8,-1),assignment,
                      current,withArgs(noe::NirOp::CheckBounds,{7,5}),
                      withArgs(noe::NirOp::Return,{}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(9);
        for(noe::Reg r:{1u,4u,7u})p.entry.registerTypes[r].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)kept=true;
        if(!check(kept,"mutated signed guard must still trap"))return 1;
    }
    {
        // Signed INT64_MIN / -1 and % -1 must retain a native division trap.
        // Constant folding these into wraparound results breaks O0/O3 parity.
        for(const auto* op:{"/","%"}) {
            noe::NirProgram p;
            auto division=withArgs(noe::NirOp::Binary,{1,2});
            division.dest=3;division.text=op;division.type.kind=noe::TypeKind::I64;
            p.entry.registerTypes.resize(4);
            p.entry.registerTypes[1].kind=noe::TypeKind::I64;
            p.entry.code={constant(1,std::numeric_limits<std::int64_t>::min()),
                          constant(2,-1),division,
                          withArgs(noe::NirOp::Return,{3})};
            noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
            bool retained=false;
            for(const auto& in:p.entry.code)
                if(in.op==noe::NirOp::Binary&&in.text==op)
                    retained=true;
            if(!check(retained,"signed overflow division trap kept at O3"))
                return 1;
        }
    }
    {
        // Guard reads OLD x, but indexing reads x after a Store before
        // the guard. No bound may be inferred from this stale comparison.
        noe::NirProgram p;
        auto stale=withArgs(noe::NirOp::Load,{});stale.dest=1;stale.text="x";
        auto write=withArgs(noe::NirOp::Store,{3});write.text="x";
        auto cmp=withArgs(noe::NirOp::Binary,{1,2});
        cmp.dest=4;cmp.text="<";cmp.type.kind=noe::TypeKind::Bool;
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{4});branch.target=9;
        auto fresh=withArgs(noe::NirOp::Load,{});fresh.dest=5;fresh.text="x";
        auto checkBounds=withArgs(noe::NirOp::CheckBounds,{5,2});
        p.entry.code={stale,constant(2,10),constant(3,99),write,cmp,branch,
                      fresh,checkBounds,withArgs(noe::NirOp::Return,{}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(6);
        p.entry.registerTypes[1].kind=noe::TypeKind::Usize;
        p.entry.registerTypes[5].kind=noe::TypeKind::Usize;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool retained=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)retained=true;
        if(!check(retained,"bounds proof rejects stale guard load"))
            return 1;
    }
    {
        // An unrelated named local may be written between the guard and
        // the access: that does not invalidate the protected index proof.
        noe::NirProgram p;
        auto index=withArgs(noe::NirOp::Load,{});index.dest=1;index.text="index";
        auto compare=withArgs(noe::NirOp::Binary,{1,2});
        compare.dest=3;compare.text="<";compare.type.kind=noe::TypeKind::Bool;
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=9;
        auto otherStore=withArgs(noe::NirOp::Store,{4});otherStore.text="other";
        auto fresh=withArgs(noe::NirOp::Load,{});fresh.dest=5;fresh.text="index";
        auto bounds=withArgs(noe::NirOp::CheckBounds,{5,2});
        p.entry.code={index,constant(2,8),compare,branch,
                      constant(4,42),otherStore,fresh,bounds,
                      withArgs(noe::NirOp::Return,{}),
                      withArgs(noe::NirOp::Return,{})};
        p.entry.registerTypes.resize(6);
        p.entry.registerTypes[1].kind=noe::TypeKind::Usize;
        p.entry.registerTypes[5].kind=noe::TypeKind::Usize;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)kept=true;
        if(!check(!kept,"unrelated store does not invalidate guard proof"))return 1;
    }
    {
        // Unknown memory writes may alias an address-taken index, so the
        // bounds check must stay even after a dominating true guard.
        noe::NirProgram p;
        auto index=withArgs(noe::NirOp::Load,{});index.dest=1;index.text="index";
        auto compare=withArgs(noe::NirOp::Binary,{1,2});
        compare.dest=3;compare.text="<";compare.type.kind=noe::TypeKind::Bool;
        auto branch=withArgs(noe::NirOp::JumpIfFalse,{3});branch.target=9;
        auto write=withArgs(noe::NirOp::StoreMemory,{4,5});
        auto fresh=withArgs(noe::NirOp::Load,{});fresh.dest=6;fresh.text="index";
        auto bounds=withArgs(noe::NirOp::CheckBounds,{6,2});
        p.entry.code={index,constant(2,8),compare,branch,
                      constant(4,0),constant(5,99),write,fresh,bounds,
                      withArgs(noe::NirOp::Return,{})};
        p.entry.code[3].target=9;
        p.entry.registerTypes.resize(7);
        p.entry.registerTypes[1].kind=noe::TypeKind::Usize;
        p.entry.registerTypes[6].kind=noe::TypeKind::Usize;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        bool kept=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::CheckBounds)kept=true;
        if(!check(kept,"unknown memory write preserves guard check"))return 1;
    }
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
        // SSA-typed local load is already represented by a Store operand.
        noe::NirProgram p;
        auto assign=withArgs(noe::NirOp::Store,{1});assign.text="count";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=2;load.text="count";
        p.entry.code={constant(1,41),assign,load,
                      withArgs(noe::NirOp::Return,{2})};
        p.entry.registerTypes.resize(3);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool foundLoad=false;
        noe::Reg returned=0;
        for(const auto& in:p.entry.code){
            if(in.op==noe::NirOp::Load)foundLoad=true;
            if(in.op==noe::NirOp::Return&&!in.args.empty())returned=in.args[0];
        }
        if(!check(!foundLoad&&returned==1,"typed straight-line mem2reg"))return 1;
    }
    {
        // Address-taken locals may change through aliases; preserve Load.
        noe::NirProgram p;
        auto assign=withArgs(noe::NirOp::Store,{1});assign.text="counter";
        auto addr=withArgs(noe::NirOp::AddressOf,{});addr.dest=2;addr.text="counter";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=3;load.text="counter";
        p.entry.code={constant(1,41),assign,addr,load,
                      withArgs(noe::NirOp::Return,{3})};
        p.entry.registerTypes.resize(4);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[3].kind=noe::TypeKind::Int;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool preserved=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load)preserved=true;
        if(!check(preserved,"address-taken local not promoted"))return 1;
    }
    {
        // Different signedness must not silently change machine op flags.
        noe::NirProgram p;
        auto assign=withArgs(noe::NirOp::Store,{1});assign.text="value";
        auto load=withArgs(noe::NirOp::Load,{});load.dest=2;load.text="value";
        p.entry.code={constant(1,1),assign,load,
                      withArgs(noe::NirOp::Return,{2})};
        p.entry.registerTypes.resize(3);
        p.entry.registerTypes[1].kind=noe::TypeKind::Int;
        p.entry.registerTypes[2].kind=noe::TypeKind::U64;
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        bool preserved=false;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Load)preserved=true;
        if(!check(preserved,"mismatched integer types not promoted"))return 1;
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
        // Duplicate arithmetic at scale must preserve the one live result.
        noe::NirProgram p;
        auto x=withArgs(noe::NirOp::Load,{});x.dest=1;x.text="x";
        auto y=withArgs(noe::NirOp::Load,{});y.dest=2;y.text="y";
        p.entry.code={x,y};
        for(noe::Reg r=3;r<263;++r){
            auto sum=withArgs(noe::NirOp::Binary,{1,2});
            sum.dest=r;sum.text="+";sum.type.kind=noe::TypeKind::I64;
            p.entry.code.push_back(sum);
        }
        p.entry.code.push_back(withArgs(noe::NirOp::Return,{262}));
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        int additions=0;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Binary&&in.text=="+")++additions;
        if(!check(additions==1,"many duplicate expressions in one block"))
            return 1;
    }
    {
        // A consumer in a following block prevents cross-block replacement.
        noe::NirProgram p;
        auto x=withArgs(noe::NirOp::Load,{});x.dest=1;x.text="x";
        auto y=withArgs(noe::NirOp::Load,{});y.dest=2;y.text="y";
        auto sum=withArgs(noe::NirOp::Binary,{1,2});
        sum.dest=3;sum.text="+";sum.type.kind=noe::TypeKind::I64;
        auto duplicate=sum;duplicate.dest=4;
        auto jump=withArgs(noe::NirOp::Jump,{});jump.target=5;
        p.entry.code={x,y,sum,duplicate,jump,withArgs(noe::NirOp::Return,{4})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);
        noe::Reg returned=0;
        for(const auto& in:p.entry.code)
            if(in.op==noe::NirOp::Return&&!in.args.empty())returned=in.args[0];
        if(!check(returned==4,"cross-block duplicate retains SSA result"))
            return 1;
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
        // Arithmetic using two preheader SSA values is loop-invariant.
        // It is nontrapping and must be evaluated before the while header,
        // with the backedge skipping its new preheader definition.
        noe::NirProgram p;
        auto left=withArgs(noe::NirOp::Load,{});left.dest=1;left.text="lhs";
        auto right=withArgs(noe::NirOp::Load,{});right.dest=2;right.text="rhs";
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=3;cond.text="run";
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{3});exit.target=7;
        auto sum=withArgs(noe::NirOp::Binary,{1,2});
        sum.dest=4;sum.type.kind=noe::TypeKind::I64;sum.text="+";
        auto write=withArgs(noe::NirOp::Store,{4});write.text="answer";
        auto back=withArgs(noe::NirOp::Jump,{});back.target=2;
        p.entry.code={left,right,cond,exit,sum,write,back,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t plus=999,guard=999,backedge=999;
        for(std::size_t k=0;k<p.entry.code.size();++k){
            const auto& in=p.entry.code[k];
            if(in.op==noe::NirOp::Binary&&in.text=="+")plus=k;
            if(in.op==noe::NirOp::JumpIfFalse)guard=k;
            if(in.op==noe::NirOp::Jump)backedge=k;
        }
        if(!check(plus<guard&&guard<backedge&&
                  p.entry.code[backedge].target>plus,
                  "O3 hoists nontrapping invariant addition"))return 1;
    }
    {
        // Variable division can trap on zero. It must stay in the loop
        // even if both SSA inputs were defined before the loop.
        noe::NirProgram p;
        auto left=withArgs(noe::NirOp::Load,{});left.dest=1;left.text="lhs";
        auto right=withArgs(noe::NirOp::Load,{});right.dest=2;right.text="rhs";
        auto cond=withArgs(noe::NirOp::Load,{});cond.dest=3;cond.text="run";
        auto exit=withArgs(noe::NirOp::JumpIfFalse,{3});exit.target=7;
        auto div=withArgs(noe::NirOp::Binary,{1,2});
        div.dest=4;div.type.kind=noe::TypeKind::I64;div.text="/";
        auto write=withArgs(noe::NirOp::Store,{4});write.text="answer";
        auto back=withArgs(noe::NirOp::Jump,{});back.target=2;
        p.entry.code={left,right,cond,exit,div,write,back,
                      withArgs(noe::NirOp::Return,{})};
        noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O3);
        std::size_t division=999,guard=999;
        for(std::size_t k=0;k<p.entry.code.size();++k){
            const auto& in=p.entry.code[k];
            if(in.op==noe::NirOp::Binary&&in.text=="/")division=k;
            if(in.op==noe::NirOp::JumpIfFalse)guard=k;
        }
        if(!check(division>guard&&division!=999,
                  "LICM must not speculatively execute trapping division"))return 1;
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
        // Candidate escape accounting must scale with large numbers of
        // nonescaping allocations without treating pointer dereferences
        // as escapes. Returning one raw pointer marks only that root.
        noe::NirFunction fn;
        for(noe::Reg r=1;r<=256;++r){
            auto root=withArgs(noe::NirOp::StackAlloc,{});
            root.dest=r;fn.code.push_back(root);
            auto read=withArgs(noe::NirOp::LoadMemory,{r});
            read.dest=r+256;fn.code.push_back(read);
        }
        fn.code.push_back(withArgs(noe::NirOp::Return,{}));
        if(!check(noe::inspectOptimizations(fn).nonEscapingStackAllocations==256,
                  "linear escape accounting for 256 nonescaping roots"))return 1;
        fn.code.back()=withArgs(noe::NirOp::Return,{1});
        if(!check(noe::inspectOptimizations(fn).nonEscapingStackAllocations==255,
                  "only returned root escapes"))return 1;
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
        // A long chain of derived pointers must inherit its stack root
        // through the worklist. Distinct nonescaping roots stay NoAlias.
        noe::NirFunction fn;
        auto root=withArgs(noe::NirOp::StackAlloc,{});
        root.dest=1;
        fn.code.push_back(root);
        for(noe::Reg r=2;r<=512;++r){
            auto next=withArgs(noe::NirOp::PtrOffset,{r-1});
            next.dest=r;
            fn.code.push_back(next);
        }
        auto other=withArgs(noe::NirOp::StackAlloc,{});
        other.dest=600;
        fn.code.push_back(other);
        auto read=withArgs(noe::NirOp::LoadMemory,{512});
        read.dest=601;
        fn.code.push_back(read);
        fn.code.push_back(withArgs(noe::NirOp::Return,{601}));
        const noe::NirAliasAnalysis analysis(fn);
        if(!check(!analysis.escapes(512)&&
                  analysis.query(1,512)==noe::AliasRelation::MayAlias&&
                  analysis.query(512,600)==noe::AliasRelation::NoAlias,
                  "worklist propagates long nonescaping pointer chains"))
            return 1;
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
