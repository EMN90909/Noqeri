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
    std::vector<std::size_t> leaders{0};
    leaders.reserve(std::min<std::size_t>(n,64));
    for(std::size_t pc=0;pc<n;++pc){
        const auto& in=fn.code[pc];
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&in.target<n)leaders.push_back(in.target);
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse||
            in.op==NirOp::Return||in.op==NirOp::Throw)&&pc+1<n)leaders.push_back(pc+1);
    }
    std::sort(leaders.begin(),leaders.end());
    leaders.erase(std::unique(leaders.begin(),leaders.end()),leaders.end());
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
    std::unordered_map<Reg,LiveInterval> intervals;
    intervals.reserve(std::min<std::size_t>(fn.nextReg,n*2));
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
    // Loop-aware allocation cost. Spilling a loop-carried value can turn a
    // single arithmetic instruction into repeated memory traffic on every
    // iteration. Weight uses inside natural backedges more heavily while
    // conservatively leaving interval/liveness construction unchanged.
    // A malformed target is ignored by the heuristic, not used to alter CFG.
    std::vector<std::uint64_t> hotUses(fn.nextReg,0);
    // Use a signed difference vector: nested backedges increase the weight
    // but cap it to avoid letting a pathological nesting count overflow.
    std::vector<int> depthDiff(n+1,0);
    for(std::size_t pc=0;pc<n;++pc){
        const auto& in=fn.code[pc];
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&
           in.target<=pc && in.target<n) {
            ++depthDiff[in.target];
            --depthDiff[pc+1];
        }
    }
    int currentDepth=0;
    for(std::size_t pc=0;pc<n;++pc){
        currentDepth+=depthDiff[pc];
        const auto weight=static_cast<std::uint64_t>(1+
            10*std::min(std::max(currentDepth,0),4));
        for(const auto reg:fn.code[pc].args)
            if(reg<hotUses.size())hotUses[reg]+=weight;
        // Weight definitions inside loops as well so loop-carried Phi/induction
        // updates are not evicted in favor of outer single-use values.
        if(fn.code[pc].dest&&*fn.code[pc].dest<hotUses.size())
            hotUses[*fn.code[pc].dest]+=weight;
    }
    std::unordered_map<Reg,std::size_t> immUses,totalUses;
    for(const auto& in:fn.code){
        for(std::size_t idx=0;idx<in.args.size();++idx){
            Reg arg=in.args[idx];
            ++totalUses[arg];
            if(idx==1&&(in.op==NirOp::Binary||in.op==NirOp::CheckBounds))
                ++immUses[arg];
        }
    }
    std::unordered_set<Reg> isImm32Const;
    for(const auto& in:fn.code){
        if(in.op==NirOp::Const&&in.dest){
            if(auto v=std::get_if<std::int64_t>(&in.literal)){
                if(*v>=std::numeric_limits<std::int32_t>::min()&&
                   *v<=std::numeric_limits<std::int32_t>::max()){
                    if(immUses[*in.dest]==totalUses[*in.dest]&&totalUses[*in.dest]>0)
                        isImm32Const.insert(*in.dest);
                }
            }
        }
    }
    const auto effectiveWeight=[&](Reg r)->std::uint64_t{
        if(isImm32Const.count(r))return 0;
        return r<hotUses.size()?hotUses[r]:0;
    };
    struct Active{std::size_t interval;std::string reg;};
    std::vector<Active> active;
    std::unordered_map<Reg,RegisterLocation> locations;
    locations.reserve(plan.intervals.size());
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
                // Prefer keeping the interval whose values are referenced
                // most often inside natural loops. On equal cost, retain the
                // original farthest-end eviction rule.
                const auto victim=std::min_element(active.begin(),active.end(),
                    [&](const auto& a,const auto& b){
                        const auto ra=plan.intervals[a.interval].value;
                        const auto rb=plan.intervals[b.interval].value;
                        const auto wa=effectiveWeight(ra);
                        const auto wb=effectiveWeight(rb);
                        if(wa!=wb)return wa<wb;
                        return plan.intervals[a.interval].end>
                               plan.intervals[b.interval].end;
                    });
                const auto victimReg=plan.intervals[victim->interval].value;
                const auto oldWeight=effectiveWeight(victimReg);
                const auto newWeight=effectiveWeight(iv.value);
                if(newWeight>oldWeight||
                   (newWeight==oldWeight&&
                    plan.intervals[victim->interval].end>iv.end)){
                    const auto evicted=plan.intervals[victim->interval];
                    const auto physical=victim->reg;
                    spill(evicted);
                    *victim=Active{current,physical};
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
