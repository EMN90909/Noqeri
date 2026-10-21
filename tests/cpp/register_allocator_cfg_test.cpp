#include "compiler.hpp"
#include "register_alloc.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

namespace noe {
// Narrow test shim: the real compiler supplies Type::size via core.cpp.
std::size_t Type::size() const { return kind==TypeKind::Void?0:8; }
}
namespace {
noe::NirInstruction make(noe::NirOp op,std::vector<noe::Reg> args={},
    std::optional<noe::Reg> dest=std::nullopt) {
    noe::NirInstruction i;i.op=op;i.args=std::move(args);i.dest=dest;return i;
}
bool ok(bool value,const char*label) {
    if(!value)std::cerr<<"register allocator: "<<label<<" FAILED\n";
    return value;
}
std::size_t end(const noe::AllocationPlan&plan,noe::Reg reg){
    for(const auto& i:plan.intervals)if(i.value==reg)return i.end;
    return 999999;
}
std::size_t count(const noe::AllocationPlan&plan,noe::Reg reg){
    return std::count_if(plan.intervals.begin(),plan.intervals.end(),
        [=](const auto&i){return i.value==reg;});
}
}
int main(){
    noe::LinearScanRegisterAllocator alloc;
    {
        // Reg 1 is read in the loop body. Liveness must include the backedge,
        // not stop at its final lexical use before the Jump instruction.
        noe::NirFunction fn;
        auto branch=make(noe::NirOp::JumpIfFalse,{2});branch.target=5;
        auto jump=make(noe::NirOp::Jump);jump.target=1;
        fn.code={
            make(noe::NirOp::Load,{},1),branch,
            make(noe::NirOp::Load,{},3),make(noe::NirOp::Store,{1,3}),
            jump,make(noe::NirOp::Return,{})
        };
        const auto p=alloc.allocate(fn,{"r12","r13"});
        if(!ok(end(p,1)>=4,"loop backedge extends input interval"))return 1;
        if(!ok(end(p,2)>=1,"live-in branch condition included"))return 1;
    }
    {
        // Multiple definitions of one virtual register are supported
        // conservatively by one union interval, not two incompatible locations.
        noe::NirFunction fn;
        fn.code={
            make(noe::NirOp::Const,{},7),
            make(noe::NirOp::Load,{},8),
            make(noe::NirOp::Store,{7,8}),
            make(noe::NirOp::Const,{},7),
            make(noe::NirOp::Return,{7})
        };
        const auto p=alloc.allocate(fn,{"r12"});
        if(!ok(count(p,7)==1&&end(p,7)==4,
            "reused virtual register merged into one live interval"))return 1;
    }
    {
        // Duplicate physical names must not provide phantom allocation slots.
        noe::NirFunction fn;
        fn.code={
            make(noe::NirOp::Const,{},1),make(noe::NirOp::Const,{},2),
            make(noe::NirOp::Const,{},3),make(noe::NirOp::Call,{1,2,3}),
            make(noe::NirOp::Return,{})
        };
        const auto p=alloc.allocate(fn,{"r12","r12"});
        const auto inRegisters=std::count_if(p.locations.begin(),p.locations.end(),
            [](const auto&l){return !l.spilled;});
        if(!ok(inRegisters==1&&p.spillSlots>=2,
            "duplicate physical registers do not overlap"))return 1;
        std::set<std::size_t> slots;
        for(const auto& l:p.locations)if(l.spilled)slots.insert(l.spillSlot);
        if(!ok(slots.size()==p.spillSlots,"spill slots individually assigned"))return 1;
    }
    return 0;
}
