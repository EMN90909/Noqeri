#include "noe.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

// Register allocation on *virtual* NIR registers. Multiple assignments to
// the same Reg share one conservative interval until a true SSA conversion
// with Phi insertion is available. Live-in/out fixed points extend intervals
// over loop backedges and conditional control-flow joins.
namespace noe {
AllocationPlan LinearScanRegisterAllocator::allocate(const NirFunction& fn,
        const std::vector<std::string>& registers) const {
    AllocationPlan plan;
    const std::size_t n=fn.code.size();
    if(!n)return plan;
    std::set<std::size_t> leaders{0};
    for(std::size_t pc=0;pc<n;++pc){
        const auto& in=fn.code[pc];
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&in.target<n)leaders.insert(in.target);
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse||
            in.op==NirOp::Return||in.op==NirOp::Throw)&&pc+1<n)leaders.insert(pc+1);
    }
    struct Block{
        std::size_t begin=0,end=0;
        std::vector<std::size_t> succ;
        std::set<Reg> use,def,in,out;
    };
    const std::vector<std::size_t> starts(leaders.begin(),leaders.end());
    std::vector<Block> blocks(starts.size());
    std::vector<std::size_t> owner(n);
    for(std::size_t b=0;b<blocks.size();++b){
        blocks[b].begin=starts[b];
        blocks[b].end=b+1<starts.size()?starts[b+1]:n;
        for(std::size_t pc=blocks[b].begin;pc<blocks[b].end;++pc)owner[pc]=b;
    }
    for(std::size_t b=0;b<blocks.size();++b){
        auto& block=blocks[b];
        auto edge=[&](std::size_t dest){
            if(dest>=n)return;
            const auto id=owner[dest];
            if(std::find(block.succ.begin(),block.succ.end(),id)==block.succ.end())
                block.succ.push_back(id);
        };
        const auto& last=fn.code[block.end-1];
        if(last.op==NirOp::Jump)edge(last.target);
        else if(last.op==NirOp::JumpIfFalse){
            edge(last.target);edge(block.end);
        }else if(last.op!=NirOp::Return&&last.op!=NirOp::Throw)edge(block.end);
        for(std::size_t pc=block.begin;pc<block.end;++pc){
            const auto& in=fn.code[pc];
            for(Reg reg:in.args)if(!block.def.count(reg))block.use.insert(reg);
            if(in.dest)block.def.insert(*in.dest);
        }
    }
    bool changed=true;
    while(changed){
        changed=false;
        for(std::size_t b=blocks.size();b-->0;){
            auto& block=blocks[b];
            std::set<Reg> out,in=block.use;
            for(auto successor:block.succ)
                out.insert(blocks[successor].in.begin(),blocks[successor].in.end());
            for(Reg reg:out)if(!block.def.count(reg))in.insert(reg);
            if(in!=block.in||out!=block.out){
                block.in.swap(in);block.out.swap(out);changed=true;
            }
        }
    }
    std::map<Reg,LiveInterval> intervals;
    auto touch=[&](Reg reg,std::size_t pc){
        auto it=intervals.find(reg);
        if(it==intervals.end()){
            LiveInterval interval;interval.value=reg;interval.begin=pc;interval.end=pc;
            if(reg<fn.registerTypes.size())interval.type=fn.registerTypes[reg];
            intervals.emplace(reg,interval);
        }else {
            it->second.begin=std::min(it->second.begin,pc);
            it->second.end=std::max(it->second.end,pc);
        }
    };
    for(std::size_t pc=0;pc<n;++pc){
        const auto& instr=fn.code[pc];
        for(Reg reg:instr.args)touch(reg,pc);
        if(instr.dest)touch(*instr.dest,pc);
    }
    for(const auto& block:blocks){
        for(Reg reg:block.in)touch(reg,block.begin);
        for(Reg reg:block.out)touch(reg,block.end-1);
    }
    // Excluding duplicate names prevents double-assigning one actual register.
    std::vector<std::string> available;
    for(const auto& reg:registers)
        if(std::find(available.begin(),available.end(),reg)==available.end())
            available.push_back(reg);
    for(auto& [reg,interval]:intervals)plan.intervals.push_back(interval);
    std::sort(plan.intervals.begin(),plan.intervals.end(),[](const auto& a,const auto& b){
        return a.begin<b.begin||(a.begin==b.begin&&a.value<b.value);
    });
    struct Active{std::size_t interval;std::string reg;};
    std::vector<Active> active;
    std::unordered_map<Reg,RegisterLocation> locations;
    auto spill=[&](const LiveInterval& iv){
        RegisterLocation loc;
        loc.value=iv.value;loc.spilled=true;loc.spillSlot=plan.spillSlots++;
        locations[iv.value]=loc;
    };
    for(std::size_t current=0;current<plan.intervals.size();++current){
        const auto& iv=plan.intervals[current];
        for(auto it=active.begin();it!=active.end();){
            if(plan.intervals[it->interval].end<iv.begin){
                available.push_back(it->reg);it=active.erase(it);
            }else ++it;
        }
        if(iv.type.size()>8||available.empty()){
            if(iv.type.size()<=8&&!active.empty()){
                auto farther=std::max_element(active.begin(),active.end(),
                    [&](const auto& a,const auto& b){
                        return plan.intervals[a.interval].end<plan.intervals[b.interval].end;
                    });
                if(plan.intervals[farther->interval].end>iv.end){
                    const auto evicted=plan.intervals[farther->interval];
                    const auto physical=farther->reg;
                    spill(evicted);
                    *farther=Active{current,physical};
                    locations[iv.value]=RegisterLocation{iv.value,physical,false,0};
                    continue;
                }
            }
            spill(iv);continue;
        }
        const auto physical=available.back();available.pop_back();
        active.push_back({current,physical});
        locations[iv.value]=RegisterLocation{iv.value,physical,false,0};
    }
    for(const auto& iv:plan.intervals)plan.locations.push_back(locations.at(iv.value));
    return plan;
}
} // namespace noe
