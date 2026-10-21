#include "noe.hpp"
#include "optimizer_cost.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
namespace noe { namespace {
bool truthy(const NirValue&v){if(auto p=std::get_if<bool>(&v))return*p;if(auto p=std::get_if<std::int64_t>(&v))return*p!=0;if(auto p=std::get_if<double>(&v))return*p!=0.0;if(auto p=std::get_if<std::string>(&v))return!p->empty();return false;}
std::int64_t wrapped(std::uint64_t value){std::int64_t out=0;std::memcpy(&out,&value,sizeof(out));return out;}
std::optional<NirValue>fold(const std::string&op,const NirValue&a,const NirValue&b){
    if(auto x=std::get_if<std::int64_t>(&a))if(auto y=std::get_if<std::int64_t>(&b)){
        if(op=="+")return NirValue(wrapped(static_cast<std::uint64_t>(*x)+static_cast<std::uint64_t>(*y)));
        if(op=="-")return NirValue(wrapped(static_cast<std::uint64_t>(*x)-static_cast<std::uint64_t>(*y)));
        if(op=="*")return NirValue(wrapped(static_cast<std::uint64_t>(*x)*static_cast<std::uint64_t>(*y)));
        if(op=="/"&&*y!=0){if(*x==std::numeric_limits<std::int64_t>::min()&&*y==-1)return NirValue(*x);return NirValue(*x / *y);}
        if(op=="%"&&*y!=0){if(*x==std::numeric_limits<std::int64_t>::min()&&*y==-1)return NirValue(std::int64_t(0));return NirValue(*x % *y);}
        if(op=="==")return NirValue(*x==*y);if(op=="!=")return NirValue(*x!=*y);if(op=="<")return NirValue(*x<*y);if(op=="<=")return NirValue(*x<=*y);if(op==">")return NirValue(*x>*y);if(op==">=")return NirValue(*x>=*y);
    }
    if(auto x=std::get_if<double>(&a))if(auto y=std::get_if<double>(&b)){if(op=="+")return NirValue(*x+*y);if(op=="-")return NirValue(*x-*y);if(op=="*")return NirValue(*x**y);if(op=="/"&&*y!=0)return NirValue(*x / *y);if(op=="==")return NirValue(*x==*y);if(op=="!=")return NirValue(*x!=*y);if(op=="<")return NirValue(*x<*y);if(op=="<=")return NirValue(*x<=*y);if(op==">")return NirValue(*x>*y);if(op==">=")return NirValue(*x>=*y);}
    if(auto x=std::get_if<std::string>(&a))if(auto y=std::get_if<std::string>(&b)){if(op=="+")return NirValue(*x+*y);if(op=="==")return NirValue(*x==*y);if(op=="!=")return NirValue(*x!=*y);}
    if(op=="&&")return NirValue(truthy(a)&&truthy(b));if(op=="||")return NirValue(truthy(a)||truthy(b));return std::nullopt;
}
bool sideEffect(const NirInstruction&i){switch(i.op){case NirOp::CheckNonNull:case NirOp::CheckBounds:case NirOp::Store:case NirOp::StoreMemory:case NirOp::Call:case NirOp::AtomicStore:case NirOp::AtomicExchange:case NirOp::AtomicCompareExchange:case NirOp::AtomicFence:case NirOp::Intrinsic:case NirOp::InlineAsm:case NirOp::Try:case NirOp::Throw:case NirOp::Jump:case NirOp::JumpIfFalse:case NirOp::Return:return true;case NirOp::LoadMemory:case NirOp::AtomicLoad:return i.isVolatile||i.op==NirOp::AtomicLoad;default:return false;}}
bool constants(NirFunction&fn){
    bool changed=false;std::unordered_map<Reg,NirValue>known;
    std::unordered_set<std::size_t> leaders{0};
    for(std::size_t index=0;index<fn.code.size();++index){
        const auto&i=fn.code[index];
        if((i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse)&&i.target<=fn.code.size())leaders.insert(i.target);
        if((i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse||i.op==NirOp::Return||i.op==NirOp::Throw)&&index+1<fn.code.size())leaders.insert(index+1);
    }
    for(std::size_t index=0;index<fn.code.size();++index){
        if(index!=0&&leaders.count(index))known.clear();
        auto&i=fn.code[index];
        if(i.op==NirOp::Const&&i.dest){known[*i.dest]=i.literal;continue;}
        if(i.op==NirOp::Binary&&i.dest&&i.args.size()==2){auto a=known.find(i.args[0]),b=known.find(i.args[1]);if(a!=known.end()&&b!=known.end())if(auto value=fold(i.text,a->second,b->second)){i.op=NirOp::Const;i.literal=*value;i.text.clear();i.args.clear();known[*i.dest]=*value;changed=true;continue;}}
        if(i.op==NirOp::CheckNonNull&&i.args.size()==1){auto a=known.find(i.args[0]);if(a!=known.end()){if(auto value=std::get_if<std::int64_t>(&a->second);value&&*value!=0){i.op=NirOp::Nop;i.args.clear();changed=true;continue;}}}
        if(i.op==NirOp::CheckBounds&&i.args.size()==2){auto a=known.find(i.args[0]),b=known.find(i.args[1]);if(a!=known.end()&&b!=known.end()){auto indexValue=std::get_if<std::int64_t>(&a->second),length=std::get_if<std::int64_t>(&b->second);if(indexValue&&length&&*indexValue>=0&&*length>=0&&*indexValue<*length){i.op=NirOp::Nop;i.args.clear();changed=true;continue;}}}
        if(i.dest)known.erase(*i.dest);
        if(i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse||i.op==NirOp::Return||i.op==NirOp::Throw)known.clear();
    }
    return changed;
}
bool dce(NirFunction&fn){
    // Conservative, transitive register liveness.  Counting reads from dead pure
    // instructions as live kept entire unused computation chains in the NIR.
    // Seed from instructions that may affect execution, then walk definitions
    // until no additional operands become live.  Global (rather than local)
    // liveness remains conservative around branches and register reuse.
    std::unordered_set<Reg>used;
    for(const auto&i:fn.code){
        if(sideEffect(i)||!i.dest)for(auto arg:i.args)used.insert(arg);
    }
    bool grew=true;
    while(grew){
        grew=false;
        for(const auto&i:fn.code){
            if(i.dest&&used.count(*i.dest)){
                for(auto arg:i.args)grew=used.insert(arg).second||grew;
            }
        }
    }
    const std::size_t oldCount=fn.code.size();
    std::vector<bool>keep(oldCount,true);
    bool changed=false;
    for(std::size_t index=0;index<oldCount;++index){
        const auto&i=fn.code[index];
        if(i.op==NirOp::Nop||(i.dest&&!used.count(*i.dest)&&!sideEffect(i))){keep[index]=false;changed=true;}
    }
    if(!changed)return false;
    std::vector<std::size_t>oldToNew(oldCount+1,0);
    std::size_t next=0;
    for(std::size_t index=0;index<oldCount;++index){oldToNew[index]=next;if(keep[index])++next;}
    oldToNew[oldCount]=next;
    std::vector<NirInstruction>kept;kept.reserve(next);
    for(std::size_t index=0;index<oldCount;++index){
        if(!keep[index])continue;
        auto instruction=std::move(fn.code[index]);
        if(instruction.op==NirOp::Jump||instruction.op==NirOp::JumpIfFalse){
            const auto target=std::min(instruction.target,oldCount);
            instruction.target=oldToNew[target];
        }
        kept.push_back(std::move(instruction));
    }
    fn.code=std::move(kept);
    return true;
}
// A graph reachability pass is safe even for back-edges. Never remove
// exceptional edges without an explicit exception control-flow graph.
bool unreachable(NirFunction& fn) {
    for (const auto& in : fn.code) {
        if (in.op == NirOp::Try || in.op == NirOp::InlineAsm) return false;
    }
    const auto n = fn.code.size();
    if (n == 0) return false;
    std::vector<bool> live(n, false);
    std::vector<std::size_t> work{0};
    while (!work.empty()) {
        const auto pc = work.back();
        work.pop_back();
        if (pc >= n || live[pc]) continue;
        live[pc] = true;
        const auto& in = fn.code[pc];
        if (in.op == NirOp::Return || in.op == NirOp::Throw) continue;
        if (in.op == NirOp::Jump) {
            if (in.target < n) work.push_back(in.target);
            continue;
        }
        if (in.op == NirOp::JumpIfFalse && in.target < n)
            work.push_back(in.target);
        if (pc + 1 < n) work.push_back(pc + 1);
    }
    std::size_t keepCount = 0;
    for (bool keep : live) if (keep) ++keepCount;
    if (keepCount == n) return false;
    std::vector<std::size_t> map(n + 1, 0);
    for (std::size_t i = 0, next = 0; i < n; ++i) {
        map[i] = next;
        if (live[i]) ++next;
    }
    map[n] = keepCount;
    std::vector<NirInstruction> compact;
    compact.reserve(keepCount);
    for (std::size_t i = 0; i < n; ++i) {
        if (!live[i]) continue;
        auto in = std::move(fn.code[i]);
        if (in.op == NirOp::Jump || in.op == NirOp::JumpIfFalse)
            in.target = map[std::min(in.target, n)];
        compact.push_back(std::move(in));
    }
    fn.code = std::move(compact);
    return true;
}
// Only boolean literal branches may be simplified. Other truthiness rules can
// differ between native and reference backends and are not speculated here.
bool foldBranches(NirFunction&fn) {
    bool changed = false;
    std::unordered_map<Reg, bool> known;
    std::unordered_set<std::size_t> leaders{0};
    for (std::size_t i = 0; i < fn.code.size(); ++i) {
        const auto& in = fn.code[i];
        if ((in.op == NirOp::Jump || in.op == NirOp::JumpIfFalse) && in.target < fn.code.size())
            leaders.insert(in.target);
        if ((in.op == NirOp::Jump || in.op == NirOp::JumpIfFalse ||
             in.op == NirOp::Return || in.op == NirOp::Throw) && i + 1 < fn.code.size())
            leaders.insert(i + 1);
    }
    for (std::size_t pc = 0; pc < fn.code.size(); ++pc) {
        if (pc != 0 && leaders.count(pc)) known.clear();
        auto& in = fn.code[pc];
        if (in.op == NirOp::Const && in.dest) {
            if (auto value = std::get_if<bool>(&in.literal)) known[*in.dest] = *value;
            else known.erase(*in.dest);
            continue;
        }
        if (in.op == NirOp::JumpIfFalse && in.args.size() == 1) {
            auto it = known.find(in.args.front());
            if (it != known.end()) {
                in.op = it->second ? NirOp::Nop : NirOp::Jump;
                in.args.clear();
                changed = true;
            }
        }
        if (in.dest) known.erase(*in.dest);
        if (in.op == NirOp::Jump || in.op == NirOp::JumpIfFalse ||
            in.op == NirOp::Return || in.op == NirOp::Throw) known.clear();
    }
    return changed;
}

// The current NIR has a linear instruction stream and jump offsets; it does
// not have Phi nodes. Require single-assignment virtual registers and genuine
// dominance before doing cross-block propagation. This is *not* general SSA
// conversion for mutable source variables (Load/Store remain effectful).
struct Graph {
    std::vector<std::pair<std::size_t,std::size_t>> spans;
    std::vector<std::size_t> owner;
    std::vector<std::vector<std::size_t>> edges, preds;
    std::vector<std::vector<bool>> dominates;
    std::vector<bool> reachable;
    bool uniqueDefs=true, validDominance=true;
    std::unordered_map<Reg,std::size_t> definitions;
};
Graph graphOf(const NirFunction& fn) {
    Graph g;
    const std::size_t n=fn.code.size();
    if(n==0)return g;
    std::set<std::size_t> leaders{0};
    for(std::size_t pc=0;pc<n;++pc){
        const auto& i=fn.code[pc];
        if((i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse)&&i.target<n)leaders.insert(i.target);
        if((i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse||i.op==NirOp::Return||i.op==NirOp::Throw)&&pc+1<n)leaders.insert(pc+1);
    }
    const std::vector<std::size_t> starts(leaders.begin(),leaders.end());
    g.owner.resize(n);
    for(std::size_t k=0;k<starts.size();++k){
        const std::size_t end=k+1<starts.size()?starts[k+1]:n;
        g.spans.push_back({starts[k],end});
        for(std::size_t pc=starts[k];pc<end;++pc)g.owner[pc]=k;
    }
    const std::size_t nb=g.spans.size();
    g.edges.resize(nb);g.preds.resize(nb);g.reachable.assign(nb,false);
    for(std::size_t b=0;b<nb;++b) {
        auto add=[&](std::size_t target) {
            if(target>=n)return;
            const std::size_t id=g.owner[target];
            if(std::find(g.edges[b].begin(),g.edges[b].end(),id)==g.edges[b].end()){
                g.edges[b].push_back(id);
                g.preds[id].push_back(b);
            }
        };
        const auto& tail=fn.code[g.spans[b].second-1];
        if(tail.op==NirOp::Jump){add(tail.target);continue;}
        if(tail.op==NirOp::JumpIfFalse){add(tail.target);add(g.spans[b].second);continue;}
        if(tail.op!=NirOp::Return&&tail.op!=NirOp::Throw)add(g.spans[b].second);
    }
    std::vector<std::size_t> work{0};
    while(!work.empty()){
        const auto b=work.back();work.pop_back();
        if(g.reachable[b])continue;
        g.reachable[b]=true;
        for(const auto succ:g.edges[b])work.push_back(succ);
    }
    g.dominates.assign(nb,std::vector<bool>(nb,true));
    for(std::size_t b=0;b<nb;++b){
        if(b==0||!g.reachable[b]){
            std::fill(g.dominates[b].begin(),g.dominates[b].end(),false);
            g.dominates[b][b]=true;
        }
    }
    bool updated=true;
    while(updated){
        updated=false;
        for(std::size_t b=1;b<nb;++b){
            if(!g.reachable[b])continue;
            std::vector<bool> next(nb,true);bool found=false;
            for(auto predecessor:g.preds[b]){
                if(!g.reachable[predecessor])continue;
                found=true;
                for(std::size_t other=0;other<nb;++other)
                    next[other]=next[other]&&g.dominates[predecessor][other];
            }
            if(!found)std::fill(next.begin(),next.end(),false);
            next[b]=true;
            if(next!=g.dominates[b]){g.dominates[b]=std::move(next);updated=true;}
        }
    }
    for(std::size_t pc=0;pc<n;++pc){
        const auto& in=fn.code[pc];
        if(in.dest&&!g.definitions.emplace(*in.dest,pc).second)g.uniqueDefs=false;
    }
    if(g.uniqueDefs){
        for(std::size_t pc=0;pc<n;++pc){
            if(!g.reachable[g.owner[pc]])continue;
            for(const auto arg:fn.code[pc].args){
                auto it=g.definitions.find(arg);
                if(it==g.definitions.end())continue; // function parameters / external values
                const auto def=it->second;
                if(def==pc || (g.owner[def]==g.owner[pc] ? def>pc :
                    !g.dominates[g.owner[pc]][g.owner[def]])){
                    g.validDominance=false;break;
                }
            }
            if(!g.validDominance)break;
        }
    }
    return g;
}
bool safeIntResult(const NirInstruction& i) {
    // No CSE for floats (NaNs, signed zero), pointers, volatile loads or
    // operations that might trap on a changed evaluation schedule.
    switch(i.type.kind) {
      case TypeKind::Int:case TypeKind::I8:case TypeKind::I16:
      case TypeKind::I32:case TypeKind::I64:case TypeKind::U8:
      case TypeKind::U16:case TypeKind::U32:case TypeKind::U64:
      case TypeKind::Isize:case TypeKind::Usize:return true;
      default:return false;
    }
}
// SCCP-style proof of constant SSA temporaries with CFG-reachability pruning.
// The absence of Phi is explicitly guarded by dominance/uniqueness. Unknown
// operands remain unknown, so no path is removed speculatively.
bool sparseConstants(NirFunction& fn) {
    const Graph g=graphOf(fn);
    if(!g.uniqueDefs||!g.validDominance)return false;
    std::unordered_map<Reg,NirValue> facts;
    bool progress=true;
    for(std::size_t step=0;step<fn.code.size()&&progress;++step){
        progress=false;
        for(const auto& i:fn.code){
            if(!i.dest||facts.count(*i.dest))continue;
            std::optional<NirValue> value;
            if(i.op==NirOp::Const&&!std::holds_alternative<std::monostate>(i.literal))value=i.literal;
            if(i.op==NirOp::Binary&&i.args.size()==2){
                const auto a=facts.find(i.args[0]),b=facts.find(i.args[1]);
                if(a!=facts.end()&&b!=facts.end())value=fold(i.text,a->second,b->second);
            }
            if(value){facts.emplace(*i.dest,*value);progress=true;}
        }
    }
    bool changed=false;
    for(auto& in:fn.code){
        if(in.dest&&(in.op==NirOp::Binary||in.op==NirOp::Unary)){
            auto it=facts.find(*in.dest);
            if(it!=facts.end()&&in.op==NirOp::Binary){
                in.op=NirOp::Const;in.literal=it->second;in.args.clear();in.text.clear();changed=true;
            }
        }
        if(in.op==NirOp::JumpIfFalse&&in.args.size()==1){
            const auto it=facts.find(in.args.front());
            if(it!=facts.end()){
                if(const auto cond=std::get_if<bool>(&it->second)){
                    in.op=*cond?NirOp::Nop:NirOp::Jump;
                    in.args.clear();changed=true;
                }
            }
        }
    }
    return changed;
}
// Local GVN: only integer arithmetic, and only when the duplicate has no use
// outside the current straight-line basic block. This avoids the need for a
// new Move instruction or cross-branch alias substitution.
bool localGvn(NirFunction& fn) {
    const Graph g=graphOf(fn);
    if(!g.uniqueDefs||!g.validDominance)return false;
    bool changed=false;
    for(const auto& span:g.spans){
        std::map<std::string,Reg> expressions;
        for(std::size_t pc=span.first;pc<span.second;++pc){
            auto& in=fn.code[pc];
            if(!in.dest||in.op!=NirOp::Binary||in.args.size()!=2||!safeIntResult(in)||
               (in.text!="+"&&in.text!="-"&&in.text!="*"&&in.text!="%"))continue;
            Reg a=in.args[0],b=in.args[1];
            if((in.text=="+"||in.text=="*")&&b<a)std::swap(a,b);
            const auto key=in.text+":"+std::to_string(static_cast<int>(in.type.kind))+":"+
                           std::to_string(a)+":"+std::to_string(b);
            const auto found=expressions.find(key);
            if(found==expressions.end()){expressions.emplace(key,*in.dest);continue;}
            const Reg redundant=*in.dest,canonical=found->second;
            bool outside=false;
            for(std::size_t later=span.second;later<fn.code.size()&&!outside;++later)
                for(const auto arg:fn.code[later].args)if(arg==redundant){outside=true;break;}
            if(outside)continue;
            for(std::size_t later=pc+1;later<span.second;++later)
                for(auto& arg:fn.code[later].args)if(arg==redundant)arg=canonical;
            in.op=NirOp::Nop;in.dest.reset();in.args.clear();changed=true;
        }
    }
    return changed;
}
// Integer identities preserve wraparound semantics and never remove the
// evaluation of operand instructions (which remain in the NIR).
bool integerStrengthReduction(NirFunction&fn) {
    const Graph g=graphOf(fn);
    if(!g.uniqueDefs||!g.validDominance)return false;
    std::unordered_map<Reg,std::int64_t> values;
    for(const auto& i:fn.code)if(i.op==NirOp::Const&&i.dest)
        if(const auto p=std::get_if<std::int64_t>(&i.literal))values[*i.dest]=*p;
    bool changed=false;
    for(auto& in:fn.code){
        if(!in.dest||in.op!=NirOp::Binary||in.args.size()!=2||!safeIntResult(in))continue;
        const auto left=values.find(in.args[0]),right=values.find(in.args[1]);
        const bool l0=left!=values.end()&&left->second==0;
        const bool r0=right!=values.end()&&right->second==0;
        const bool r1=right!=values.end()&&right->second==1;
        const bool knownZero=(in.text=="*"&&(l0||r0))||
                             (in.text=="%"&&r1);
        if(!knownZero)continue;
        // Avoid rewriting instructions that might have typed checked
        // arithmetic semantics different from our wraparound i64 fold.
        if(in.type.kind!=TypeKind::Int&&in.type.kind!=TypeKind::I64)continue;
        in.op=NirOp::Const;in.literal=std::int64_t{0};
        in.args.clear();in.text.clear();changed=true;
    }
    return changed;
}
// Instruction scheduling (register-pressure form): sink constant definitions
// inside one basic block to immediately before their first local use. No
// branch or exception moves; no change to the order of effectful operations.
// Unused and externally consumed constants stay put.
bool scheduleLocalConstants(NirFunction& fn) {
    const Graph g=graphOf(fn);
    if(!g.uniqueDefs||!g.validDominance)return false;
    bool changed=false;
    for(const auto& span:g.spans){
        for(std::size_t pc=span.first;pc<span.second;++pc){
            const auto& in=fn.code[pc];
            if(in.op!=NirOp::Const||!in.dest||
               !(std::holds_alternative<std::int64_t>(in.literal)||
                 std::holds_alternative<bool>(in.literal)))continue;
            const Reg output=*in.dest;
            std::size_t first=span.second;
            for(std::size_t next=pc+1;next<span.second;++next) {
                if(std::find(fn.code[next].args.begin(),fn.code[next].args.end(),output)
                    !=fn.code[next].args.end()) {first=next;break;}
            }
            if(first<=pc+1||first>=span.second)continue;
            // Rotating the literal to first-1 does not alter instruction
            // indices, branch destinations, or relative order of effects.
            std::rotate(fn.code.begin()+static_cast<std::ptrdiff_t>(pc),
                        fn.code.begin()+static_cast<std::ptrdiff_t>(pc+1),
                        fn.code.begin()+static_cast<std::ptrdiff_t>(first));
            changed=true;
        }
    }
    return changed;
}
// Effect-free, dependency-aware local instruction scheduling. This is a
// bounded integer NIR DAG scheduler, not a target-machine scheduler. It does
// not move operations across loads, stores, calls, branches, traps or atomics.
// Longer arithmetic dependency chains are scheduled before shorter ones.
bool scheduleIntegerDag(NirFunction& fn) {
    const auto g=graphOf(fn);
    if(!g.uniqueDefs||!g.validDominance)return false;
    for(const auto& in:fn.code)
        if(in.op==NirOp::Try||in.op==NirOp::InlineAsm)return false;
    const auto eligible=[](const NirInstruction& in) {
        if(!in.dest||in.isVolatile)return false;
        if(in.op==NirOp::Const)
            return std::holds_alternative<std::int64_t>(in.literal)||
                   std::holds_alternative<bool>(in.literal);
        if(in.op==NirOp::Binary&&safeIntResult(in))
            return in.text=="+"||in.text=="-"||in.text=="*";
        if(in.op==NirOp::Unary&&safeIntResult(in))
            return in.text=="-"||in.text=="!";
        return false;
    };
    bool changed=false;
    for(const auto& block:g.spans){
        std::size_t first=block.first;
        while(first<block.second){
            if(!eligible(fn.code[first])){++first;continue;}
            std::size_t end=first;
            while(end<block.second&&eligible(fn.code[end])&&end-first<32)++end;
            const auto count=end-first;
            if(count<3){first=end;continue;}
            std::unordered_map<Reg,std::size_t> producer;
            for(std::size_t i=0;i<count;++i)
                producer[*fn.code[first+i].dest]=i;
            std::vector<std::vector<std::size_t>> consumers(count);
            std::vector<std::size_t> indegree(count,0);
            bool valid=true;
            for(std::size_t i=0;i<count;++i){
                for(const Reg arg:fn.code[first+i].args){
                    const auto it=producer.find(arg);
                    if(it==producer.end())continue;
                    if(it->second>=i){valid=false;break;}
                    consumers[it->second].push_back(i);++indegree[i];
                }
                if(!valid)break;
            }
            if(!valid){first=end;continue;}
            std::vector<unsigned> path(count,0);
            for(std::size_t i=count;i-->0;){
                const auto& in=fn.code[first+i];
                unsigned cost=in.op==NirOp::Binary&&in.text=="*"?3u:1u;
                unsigned longest=0;
                for(auto consumer:consumers[i])
                    longest=std::max(longest,path[consumer]);
                path[i]=cost+longest;
            }
            std::vector<std::size_t> order;order.reserve(count);
            std::vector<bool> scheduled(count,false);
            for(std::size_t step=0;step<count;++step){
                std::size_t candidate=count;
                for(std::size_t i=0;i<count;++i){
                    if(scheduled[i]||indegree[i])continue;
                    if(candidate==count||path[i]>path[candidate]||
                       (path[i]==path[candidate]&&i<candidate))
                        candidate=i;
                }
                if(candidate==count){valid=false;break;}
                scheduled[candidate]=true;order.push_back(candidate);
                for(auto user:consumers[candidate])--indegree[user];
            }
            if(valid){
                bool differs=false;
                for(std::size_t i=0;i<count;++i)
                    if(order[i]!=i){differs=true;break;}
                if(differs){
                    std::vector<NirInstruction> reordered;
                    reordered.reserve(count);
                    for(auto i:order)reordered.push_back(fn.code[first+i]);
                    for(std::size_t i=0;i<count;++i)
                        fn.code[first+i]=std::move(reordered[i]);
                    changed=true;
                }
            }
            first=end;
        }
    }
    return changed;
}
// LICM for a single-entry *structured* natural loop. Only hoists Const,
// which cannot trap, allocate, alias or change numerical behavior. Any branch
// inside the body, exceptional operation or external entry blocks movement.
bool hoistLoopLiteral(NirFunction& fn) {
    const Graph analysis=graphOf(fn);
    if(!analysis.uniqueDefs||!analysis.validDominance)return false;
    const std::size_t n=fn.code.size();
    if(n<5)return false;
    for(std::size_t back=0;back<n;++back){
        if(fn.code[back].op!=NirOp::Jump||fn.code[back].target>=back)continue;
        const std::size_t header=fn.code[back].target;
        bool external=false;
        for(std::size_t k=0;k<n;++k){
            const auto& i=fn.code[k];
            if(i.op==NirOp::Try||i.op==NirOp::InlineAsm)return false;
            if(k>=header&&k<=back)continue;
            // Entering anywhere inside the loop without traversing the
            // preheader would violate the moved definition's dominance.
            if((i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse)&&
               i.target>=header&&i.target<=back)external=true;
        }
        if(external)continue;
        std::size_t branch=n;bool structured=true;
        for(std::size_t k=header;k<back;++k){
            const auto& in=fn.code[k];
            if(in.op==NirOp::JumpIfFalse){
                if(branch!=n||in.target<=back){structured=false;break;}
                branch=k;continue;
            }
            if(in.op==NirOp::Jump||in.op==NirOp::Return||in.op==NirOp::Throw){
                structured=false;break;
            }
        }
        if(!structured||branch==n)continue;
        for(std::size_t candidate=branch+1;candidate<back;++candidate){
            const auto& instruction=fn.code[candidate];
            if(instruction.op!=NirOp::Const||!instruction.dest)continue;
            const auto reg=*instruction.dest;
            bool outside=false;
            for(std::size_t k=0;k<n&&!outside;++k)
                if(k<header||k>back)for(Reg arg:fn.code[k].args)
                    if(arg==reg){outside=true;break;}
            if(outside)continue;
            const auto saved=instruction;
            fn.code[candidate].op=NirOp::Nop;
            fn.code[candidate].dest.reset();
            fn.code[candidate].args.clear();
            fn.code.insert(fn.code.begin()+static_cast<std::ptrdiff_t>(header),saved);
            // The backedge must bypass the new preheader constant, but the
            // initial fallthrough executes it once.
            for(std::size_t k=0;k<fn.code.size();++k){
                auto& in=fn.code[k];
                if(in.op!=NirOp::Jump&&in.op!=NirOp::JumpIfFalse)continue;
                const std::size_t oldTarget=in.target;
                if(oldTarget>=header){
                    if(oldTarget==header&&k>header&&k<=back+1)in.target=header+1;
                    else in.target=oldTarget+1;
                }
            }
            return true;
        }
    }
    return false;
}
// A semantics-preserving 2x loop unroller for structured while loops.
// It duplicates the original condition and body, testing the exit condition
// between the two iterations. No trip count is guessed. Unknown control-flow
// entries, exceptional edges and nested loops are rejected conservatively.
// Run only after SSA-dependent passes: repeating a virtual register's
// definitions across two unrolled iterations invalidates single-def SSA.
bool unrollStructuredLoop2x(NirFunction& fn) {
    const std::size_t n=fn.code.size();
    if(n<4 || n>4096) return false;
    for(const auto& in:fn.code)
        if(in.op==NirOp::Try || in.op==NirOp::InlineAsm ||
           in.op==NirOp::Throw) return false;
    for(std::size_t back=0;back<n;++back){
        const auto& backedge=fn.code[back];
        if(backedge.op!=NirOp::Jump||backedge.target>=back)continue;
        const std::size_t header=backedge.target,span=back-header;
        if(span<2 || span>20 || n+span>4096)continue;
        std::size_t condition=n;
        bool structured=true;
        for(std::size_t i=header;i<back;++i){
            const auto& in=fn.code[i];
            if(in.op==NirOp::JumpIfFalse){
                if(condition!=n||in.target!=back+1||in.args.size()!=1){
                    structured=false;break;
                }
                condition=i;
            } else if(in.op==NirOp::Jump||in.op==NirOp::Return||
                      in.op==NirOp::Throw||in.op==NirOp::Try||
                      in.op==NirOp::InlineAsm){
                structured=false;break;
            }
        }
        if(!structured||condition==n || condition==back-1)continue;
        // Only the loop's own backedge may jump into the repeated region.
        // Incoming edges to its header remain legal.
        for(std::size_t i=0;i<n&&structured;++i){
            if(i>=header && i<=back)continue;
            const auto& in=fn.code[i];
            if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&
                in.target>header && in.target<=back) structured=false;
        }
        if(!structured)continue;
        // Other backedges would make the cloned loop's entry/restart
        // behavior ambiguous. Defer nested and multi-exit loops.
        for(std::size_t i=0;i<n&&structured;++i)
            if(i!=back&&fn.code[i].op==NirOp::Jump&&
               fn.code[i].target<i)structured=false;
        if(!structured)continue;
        const auto copyBegin=fn.code.begin()+static_cast<std::ptrdiff_t>(header);
        const auto copyEnd=fn.code.begin()+static_cast<std::ptrdiff_t>(back);
        std::vector<NirInstruction> iteration(copyBegin,copyEnd);
        // Validate the complete output before mutating the original.
        std::vector<NirInstruction> out;
        out.reserve(n+span);
        out.insert(out.end(),fn.code.begin(),
                   fn.code.begin()+static_cast<std::ptrdiff_t>(back));
        out.insert(out.end(),iteration.begin(),iteration.end());
        out.insert(out.end(),fn.code.begin()+static_cast<std::ptrdiff_t>(back),
                   fn.code.end());
        // Translate old jump targets once, including branches outside loop.
        // The replicated exit checks both lead to exactly old back+1.
        for(std::size_t i=0;i<out.size();++i){
            auto& in=out[i];
            if(in.op!=NirOp::Jump&&in.op!=NirOp::JumpIfFalse)continue;
            if(i>=back && i<back+span) {
                // Every cloned control edge is the unique loop exit.
                in.target=back+1+span;
            }else if(in.target>=back){
                if(in.target>n)return false;
                in.target+=span;
            }
        }
        fn.code=std::move(out);
        return true;
    }
    return false;
}
void pipeline(NirFunction& fn, OptimizationLevel level) {
    if (level == OptimizationLevel::O0) return;
    // Eliminate duplicate expressions before conventional DCE discards their
    // first (temporarily unused) producer. Pass order is observable here.
    if(level==OptimizationLevel::O2||level==OptimizationLevel::O3||
       level==OptimizationLevel::Oz) localGvn(fn);
    const int rounds = level == OptimizationLevel::O3 ? 3 :
                       level == OptimizationLevel::O2 ? 2 : 1;
    for (int r = 0; r < rounds; ++r) {
        const bool a = constants(fn);
        const bool b = dce(fn);
        if (!a && !b) break;
    }
    // Gate new branch simplification at O2+. O1 retains legacy behavior.
    if (level == OptimizationLevel::O2 || level == OptimizationLevel::O3 ||
        level == OptimizationLevel::Oz) {
        for (int iteration = 0; iteration < 3; ++iteration) {
            const bool a = sparseConstants(fn);
            const bool b = foldBranches(fn);
            const bool c = unreachable(fn);
            const bool d = localGvn(fn);
            const bool e = integerStrengthReduction(fn);
            const bool f = constants(fn);
            const bool g = dce(fn);
            if (!a && !b && !c && !d && !e && !f && !g) break;
        }
    }
    if(level==OptimizationLevel::O3) scheduleLocalConstants(fn);
    if(level==OptimizationLevel::O3 && hoistLoopLiteral(fn)) {
        // Hoisting changes jump indices: the existing branch compaction pass
        // rechecks all targets and dead definitions after transformation.
        dce(fn);
    }
    if(level==OptimizationLevel::O3) scheduleIntegerDag(fn);
    // Late unrolling preserves each condition evaluation and all observable
    // effects; no earlier SSA-only pass is rerun on the replicated registers.
    if(level==OptimizationLevel::O3) unrollStructuredLoop2x(fn);
    if (level == OptimizationLevel::Oz) dce(fn);
}
}
NirAliasAnalysis::NirAliasAnalysis(const NirFunction& fn) {
    const auto cfg=graphOf(fn);
    if(!cfg.uniqueDefs||!cfg.validDominance)return;
    ssaValid_=true;
    for(std::size_t b=0;b<cfg.edges.size();++b)
        for(auto to:cfg.edges[b])if(to<=b)hasBackedge_=true;
    for(const auto& i:fn.code)
        if(i.op==NirOp::StackAlloc&&i.dest)roots_[*i.dest]=*i.dest;
    bool changed=true;
    while(changed){
        changed=false;
        for(const auto& i:fn.code){
            if(!i.dest||i.args.empty()||roots_.count(*i.dest))continue;
            if(i.op!=NirOp::PtrOffset&&i.op!=NirOp::MakeSlice&&i.op!=NirOp::SliceData)continue;
            const auto it=roots_.find(i.args[0]);
            if(it!=roots_.end()){roots_[*i.dest]=it->second;changed=true;}
        }
    }
    for(const auto& i:fn.code) {
        for(std::size_t p=0;p<i.args.size();++p){
            const auto it=roots_.find(i.args[p]);
            if(it==roots_.end())continue;
            if(p==0&&(i.op==NirOp::LoadMemory||i.op==NirOp::CheckNonNull||
                i.op==NirOp::StoreMemory||i.op==NirOp::PtrOffset||
                i.op==NirOp::MakeSlice||i.op==NirOp::SliceData))continue;
            escapingRoots_.insert(it->second);
        }
    }
}
bool NirAliasAnalysis::escapes(Reg pointer) const {
    const auto it=roots_.find(pointer);
    return it==roots_.end() || escapingRoots_.count(it->second)>0;
}
AliasRelation NirAliasAnalysis::query(Reg a, Reg b) const {
    if(a==b)return ssaValid_ ? AliasRelation::MustAlias : AliasRelation::MayAlias;
    const auto left=roots_.find(a),right=roots_.find(b);
    if(left==roots_.end()||right==roots_.end()||hasBackedge_)
        return AliasRelation::MayAlias;
    if(left->second!=right->second && !escapingRoots_.count(left->second) &&
       !escapingRoots_.count(right->second))return AliasRelation::NoAlias;
    return AliasRelation::MayAlias;
}
// Read-only optimizer readiness evidence: candidate counts never claim a pass
// actually vectorized, removed an allocation or changed a target schedule.
OptimizationCapabilities inspectOptimizations(const NirFunction& fn) {
    const Graph g=graphOf(fn); OptimizationCapabilities out;
    out.uniqueVirtualRegisterDefinitions=g.uniqueDefs;
    out.allDefinitionsDominateUses=g.validDominance&&g.uniqueDefs;
    out.basicBlocks=g.spans.size();
    for(std::size_t b=0;b<g.edges.size();++b)
        for(std::size_t succ:g.edges[b])if(succ<=b&&g.dominates[b][succ])
            ++out.naturalBackedges;
    std::unordered_set<Reg> pointerRoots;
    for(const auto& i:fn.code)if(i.op==NirOp::StackAlloc&&i.dest)pointerRoots.insert(*i.dest);
    // Conservative escape analysis: only direct stack addresses that never
    // participate in unknown use sites count as nonescaping candidates.
    for(const auto root:pointerRoots){
        bool escapes=false;
        for(const auto& i:fn.code)for(std::size_t a=0;a<i.args.size();++a){
            if(i.args[a]!=root)continue;
            if((i.op==NirOp::LoadMemory||i.op==NirOp::CheckNonNull)&&a==0)continue;
            if(i.op==NirOp::StoreMemory&&a==0)continue;
            escapes=true;
        }
        if(!escapes)++out.nonEscapingStackAllocations;
    }
    for(const auto& i:fn.code){
        if(i.op==NirOp::Binary && safeIntResult(i) && (i.text=="+"||i.text=="*"))
            ++out.candidateVectorOperations;
        if(i.op==NirOp::Const&&i.dest){
            for(std::size_t pc=0;pc<fn.code.size();++pc) {
                const auto& tail=fn.code[pc];
                if(tail.op==NirOp::Jump&&tail.target<pc){
                    if(i.dest && pc>tail.target && pc<fn.code.size())
                        if((&i>=&fn.code[tail.target])&&(&i<&fn.code[pc]))
                            ++out.candidateLoopConstants;
                }
            }
        }
    }
    return out;
}
// This deliberately narrow inliner substitutes *only* a side-effect-free,
// zero-argument function consisting of one constant and its return.
// It must never convert a potentially throwing or effectful call.
void PassManager::optimize(NirProgram& p) const {
    if (level_ == OptimizationLevel::O2 || level_ == OptimizationLevel::O3 ||
        level_ == OptimizationLevel::Oz) {
        // Interprocedural constant evaluation for bounded, zero-argument,
        // non-extern, side-effect-free function bodies. This is not general
        // call inlining: no memory operations, exceptions, parameters, or
        // recursive calls are evaluated at compile time.
        std::unordered_map<std::string, unsigned> definitions;
        for(const auto& fn:p.functions)
            if(!fn.name.empty())++definitions[fn.name];
        std::unordered_map<std::string,NirValue> candidates;
        for(std::size_t round=0;round<p.functions.size();++round) {
            bool progressed=false;
            for(const auto& fn:p.functions) {
                if(fn.name.empty()||definitions[fn.name]!=1||fn.isExtern||
                   !fn.params.empty()||!fn.paramTypes.empty()||
                   fn.code.empty()||fn.code.size()>12||
                   candidates.count(fn.name))continue;
                std::unordered_map<Reg,NirValue> values;
                bool valid=true,returned=false;
                NirValue result;
                std::unordered_set<Reg> defs;
                for(std::size_t pc=0;pc<fn.code.size();++pc) {
                    const auto& in=fn.code[pc];
                    if(in.dest&&!defs.insert(*in.dest).second){valid=false;break;}
                    if(in.op==NirOp::Const&&in.dest&&
                       !std::holds_alternative<std::monostate>(in.literal)) {
                        values[*in.dest]=in.literal;
                    }else if(in.op==NirOp::Binary&&in.dest&&in.args.size()==2) {
                        const auto a=values.find(in.args[0]),b=values.find(in.args[1]);
                        if(a==values.end()||b==values.end()||
                           !std::holds_alternative<std::int64_t>(a->second)||
                           !std::holds_alternative<std::int64_t>(b->second)||
                           (in.text!="+"&&in.text!="-"&&in.text!="*"&&
                            in.text!="=="&&in.text!="!="&&in.text!="<"&&
                            in.text!="<="&&in.text!=">"&&in.text!=">=")) {
                            valid=false;break;
                        }
                        auto folded=fold(in.text,a->second,b->second);
                        if(!folded){valid=false;break;}
                        values[*in.dest]=*folded;
                    }else if(in.op==NirOp::Call&&in.dest&&in.args.empty()) {
                        const auto callee=candidates.find(in.text);
                        if(callee==candidates.end()){valid=false;break;}
                        values[*in.dest]=callee->second;
                    }else if(in.op==NirOp::Return&&in.args.size()==1&&
                             pc+1==fn.code.size()) {
                        const auto value=values.find(in.args[0]);
                        if(value==values.end()){valid=false;break;}
                        result=value->second;returned=true;
                    }else {valid=false;break;}
                }
                if(valid&&returned){
                    candidates.emplace(fn.name,result);
                    progressed=true;
                }
            }
            if(!progressed)break;
        }
        auto inlineConstants=[&](NirFunction& fn) {
            for(auto& in:fn.code) {
                if(in.op!=NirOp::Call||!in.dest||!in.args.empty())continue;
                const auto it=candidates.find(in.text);
                if(it==candidates.end())continue;
                in.op=NirOp::Const;
                in.literal=it->second;
                in.text.clear();
            }
        };
        inlineConstants(p.entry);
        for(auto& fn:p.functions)inlineConstants(fn);
    }
    pipeline(p.entry, level_);
    for (auto& fn : p.functions) pipeline(fn, level_);
}
void Optimizer::optimize(NirProgram&p)const{optimize(p,OptimizationLevel::O1);}void Optimizer::optimize(NirProgram&p,OptimizationLevel level)const{PassManager(level).optimize(p);}void Optimizer::optimizeFunction(NirFunction&fn)const{pipeline(fn,OptimizationLevel::O1);} } // namespace noe
