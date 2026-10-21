#include "noe.hpp"
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

// SSA-guarded local GVN. This IR does not have Phi nodes or a dominance-aware
// SSA converter yet, so only an entire single straight-line register-SSA
// function may be rewritten. No memory values or trapping operations are CSE'd.
bool localGvn(NirFunction& fn) {
    std::unordered_set<Reg> defined;
    for (const auto& in : fn.code) {
        if (in.op == NirOp::Jump || in.op == NirOp::JumpIfFalse ||
            in.op == NirOp::Try || in.op == NirOp::InlineAsm ||
            in.op == NirOp::Throw || in.isVolatile) return false;
        if (in.dest && !defined.insert(*in.dest).second) return false;
    }
    std::unordered_map<std::string, Reg> seen;
    std::unordered_map<Reg, Reg> replacements;
    auto canonical = [&](Reg r) {
        std::unordered_set<Reg> guard;
        while (replacements.count(r) && guard.insert(r).second)
            r = replacements.at(r);
        return r;
    };
    bool changed = false;
    for (auto& in : fn.code) {
        for (auto& r : in.args) {
            const auto next = canonical(r);
            if (next != r) { r = next; changed = true; }
        }
        if (in.op != NirOp::Binary || !in.dest || in.args.size() != 2)
            continue;
        // These operators are pure and have no divide-by-zero or other
        // exceptional value constraints. Floating calculations are excluded
        // until the frontend models rounding mode and FP status flags.
        const auto integer = [](TypeKind k) {
            switch (k) {
                case TypeKind::Int: case TypeKind::I8: case TypeKind::I16:
                case TypeKind::I32: case TypeKind::I64: case TypeKind::U8:
                case TypeKind::U16: case TypeKind::U32: case TypeKind::U64:
                case TypeKind::Isize: case TypeKind::Usize: return true;
                default: return false;
            }
        };
        if (in.args[0] >= fn.registerTypes.size() ||
            in.args[1] >= fn.registerTypes.size() ||
            !integer(fn.registerTypes[in.args[0]].kind) ||
            !integer(fn.registerTypes[in.args[1]].kind) ||
            !(integer(in.type.kind) || in.type.kind == TypeKind::Bool)) continue;
        if (in.text != "+" && in.text != "-" && in.text != "*" &&
            in.text != "==" && in.text != "!=" && in.text != "<" &&
            in.text != "<=" && in.text != ">" && in.text != ">=" &&
            in.text != "&&" && in.text != "||") continue;
        auto lhs = in.args[0], rhs = in.args[1];
        if (in.text == "+" || in.text == "*" || in.text == "==" ||
            in.text == "!=" || in.text == "&&" || in.text == "||")
            if (rhs < lhs) std::swap(lhs, rhs);
        const std::string key = in.text + ":" + std::to_string(lhs) +
            ":" + std::to_string(rhs) + ":" + std::to_string(in.width) +
            ":" + std::to_string(static_cast<int>(in.type.kind));
        const auto found = seen.find(key);
        if (found == seen.end()) {
            seen.emplace(key, *in.dest);
        } else {
            replacements[*in.dest] = found->second;
            in.op = NirOp::Nop;
            in.args.clear();
            in.dest.reset();
            changed = true;
        }
    }
    return changed;
}
void pipeline(NirFunction& fn, OptimizationLevel level) {
    if (level == OptimizationLevel::O0) return;
    const int rounds = level == OptimizationLevel::O3 ? 3 :
                       level == OptimizationLevel::O2 ? 2 : 1;
    for (int r = 0; r < rounds; ++r) {
        const bool a = constants(fn);
        const bool g = (level == OptimizationLevel::O2 ||
                        level == OptimizationLevel::O3 ||
                        level == OptimizationLevel::Oz) && localGvn(fn);
        const bool b = dce(fn);
        if (!a && !g && !b) break;
    }
    // Gate new branch simplification at O2+. O1 retains legacy behavior.
    if (level == OptimizationLevel::O2 || level == OptimizationLevel::O3 ||
        level == OptimizationLevel::Oz) {
        for (int iteration = 0; iteration < 3; ++iteration) {
            const bool a = foldBranches(fn);
            const bool b = unreachable(fn);
            const bool c = constants(fn);
            const bool g = localGvn(fn);
            const bool d = dce(fn);
            if (!a && !b && !c && !g && !d) break;
        }
    }
    if (level == OptimizationLevel::Oz) dce(fn);
}
}
// This deliberately narrow inliner substitutes *only* a side-effect-free,
// zero-argument function consisting of one constant and its return.
// It must never convert a potentially throwing or effectful call.
void PassManager::optimize(NirProgram& p) const {
    if (level_ == OptimizationLevel::O2 || level_ == OptimizationLevel::O3 ||
        level_ == OptimizationLevel::Oz) {
        std::unordered_map<std::string, NirValue> candidates;
        std::unordered_set<std::string> ambiguous;
        for (const auto& fn : p.functions) {
            if (fn.name.empty() || fn.isExtern || !fn.params.empty() ||
                !fn.paramTypes.empty() || fn.code.size() != 2) continue;
            const auto& a = fn.code[0];
            const auto& b = fn.code[1];
            if (a.op != NirOp::Const || !a.dest ||
                std::holds_alternative<std::monostate>(a.literal) ||
                b.op != NirOp::Return || b.args.size() != 1 ||
                b.args.front() != *a.dest) continue;
            if (!candidates.emplace(fn.name, a.literal).second)
                ambiguous.insert(fn.name);
        }
        for (const auto& name : ambiguous) candidates.erase(name);
        auto inlineConstants = [&](NirFunction& fn) {
            for (auto& in : fn.code) {
                if (in.op != NirOp::Call || !in.dest || !in.args.empty()) continue;
                auto found = candidates.find(in.text);
                if (found == candidates.end()) continue;
                in.op = NirOp::Const;
                in.literal = found->second;
                in.text.clear();
            }
        };
        inlineConstants(p.entry);
        for (auto& fn : p.functions) inlineConstants(fn);
    }
    pipeline(p.entry, level_);
    for (auto& fn : p.functions) pipeline(fn, level_);
}
void Optimizer::optimize(NirProgram&p)const{optimize(p,OptimizationLevel::O1);}void Optimizer::optimize(NirProgram&p,OptimizationLevel level)const{PassManager(level).optimize(p);}void Optimizer::optimizeFunction(NirFunction&fn)const{pipeline(fn,OptimizationLevel::O1);} } // namespace noe
