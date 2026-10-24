#include "noe.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace noe {
namespace {
Type simple(TypeKind k){Type t;t.kind=k;return t;}
Type constType(const NirValue&v){if(std::holds_alternative<std::int64_t>(v))return simple(TypeKind::Int);if(std::holds_alternative<double>(v))return simple(TypeKind::Float);if(std::holds_alternative<bool>(v))return simple(TypeKind::Bool);if(std::holds_alternative<std::string>(v))return simple(TypeKind::String);return simple(TypeKind::Null);}
bool comparison(const std::string&op){return op=="=="||op=="!="||op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||";}
Type regType(const NirFunction&fn,Reg r){return r<fn.registerTypes.size()?fn.registerTypes[r]:Type{};}
void ensureReg(NirFunction&fn,Reg r){if(fn.registerTypes.size()<=r)fn.registerTypes.resize(static_cast<std::size_t>(r)+1);}
void blocks(NirFunction&fn){
    fn.blocks.clear();if(fn.code.empty())return;std::set<std::size_t>leaders{0};
    for(std::size_t i=0;i<fn.code.size();++i){const auto&in=fn.code[i];if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&in.target<fn.code.size())leaders.insert(in.target);if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse||in.op==NirOp::Return||in.op==NirOp::Throw)&&i+1<fn.code.size())leaders.insert(i+1);}
    std::vector<std::size_t>starts(leaders.begin(),leaders.end());std::unordered_map<std::size_t,BlockId>byPc;
    for(std::size_t i=0;i<starts.size();++i){NirBasicBlock b;b.id=static_cast<BlockId>(i);b.begin=starts[i];b.end=i+1<starts.size()?starts[i+1]:fn.code.size();fn.blocks.push_back(b);byPc[b.begin]=b.id;}
    auto blockAt=[&](std::size_t pc)->std::optional<BlockId>{auto exact=byPc.find(pc);if(exact!=byPc.end())return exact->second;for(const auto&b:fn.blocks)if(pc>=b.begin&&pc<b.end)return b.id;return std::nullopt;};
    for(std::size_t i=0;i<fn.blocks.size();++i){auto&b=fn.blocks[i];if(b.begin>=b.end)continue;const auto&last=fn.code[b.end-1];auto add=[&](std::optional<BlockId>id){if(id&&std::find(b.successors.begin(),b.successors.end(),*id)==b.successors.end())b.successors.push_back(*id);};if(last.op==NirOp::Jump){add(blockAt(last.target));continue;}if(last.op==NirOp::JumpIfFalse){add(blockAt(last.target));if(i+1<fn.blocks.size())add(fn.blocks[i+1].id);continue;}if(last.op==NirOp::Return||last.op==NirOp::Throw)continue;if(i+1<fn.blocks.size())add(fn.blocks[i+1].id);}
}
}

void NirAnalyzer::analyze(const Program&program,NirProgram&nir)const{
    std::unordered_map<std::string,std::shared_ptr<FunctionStmt>>astFunctions;std::unordered_map<std::string,Type>returns;
    for(const auto&s:program.statements)if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s)){astFunctions[f->name]=f;returns[f->name]=f->returnType?typeFromName(*f->returnType):simple(TypeKind::Void);}
    auto analyzeFunction=[&](NirFunction&fn){
        fn.registerTypes.assign(fn.nextReg,Type{});std::unordered_map<std::string,Type>variables;
        if(auto it=astFunctions.find(fn.name);it!=astFunctions.end()){
            const auto&af=it->second;fn.paramTypes.clear();for(const auto&p:af->params){auto t=p.annotation?typeFromName(*p.annotation):Type{};fn.paramTypes.push_back(t);variables[p.name]=t;}fn.resultType=af->returnType?typeFromName(*af->returnType):simple(TypeKind::Void);
        }else{for(std::size_t i=0;i<fn.params.size()&&i<fn.paramTypes.size();++i)variables[fn.params[i]]=fn.paramTypes[i];}
        for(auto&in:fn.code){Type t;switch(in.op){
            case NirOp::Const:t=constType(in.literal);break;
            case NirOp::Load:{auto it=variables.find(in.text);if(it!=variables.end())t=it->second;break;}
            case NirOp::Store:if(!in.args.empty())variables[in.text]=regType(fn,in.args[0]);break;
            case NirOp::Unary:t=in.text=="!"?simple(TypeKind::Bool):(in.args.empty()?Type{}:regType(fn,in.args[0]));break;
            case NirOp::Binary:t=comparison(in.text)?simple(TypeKind::Bool):(in.args.empty()?Type{}:regType(fn,in.args[0]));break;
            case NirOp::Cast:t=typeFromName(in.text);break;
            case NirOp::Call:{auto it=returns.find(in.text);if(it!=returns.end())t=it->second;else if(in.text=="clockMillis")t=simple(TypeKind::I64);else if(in.text=="platform")t=simple(TypeKind::String);else if(in.text=="textLength"||in.text=="len")t=simple(TypeKind::Usize);else if(in.text=="print")t=simple(TypeKind::Void);break;}
            case NirOp::AddressOf:{t.kind=TypeKind::Pointer;auto it=variables.find(in.text);if(it!=variables.end())t.pointee=std::make_shared<Type>(it->second);break;}
            case NirOp::LoadMemory:
                // The type checker knows the dereferenced field/array
                // element type. The generic byte-oriented StackAlloc pointer
                // must NOT override that signedness/width metadata.
                if(in.type.kind!=TypeKind::Unknown)t=in.type;
                else if(!in.args.empty()){
                    auto p=regType(fn,in.args[0]);
                    if(p.kind==TypeKind::Pointer&&p.pointee)t=*p.pointee;
                }
                break;
            case NirOp::PtrOffset:if(!in.args.empty())t=regType(fn,in.args[0]);break;
            case NirOp::StackAlloc:t.kind=TypeKind::Pointer;t.pointee=std::make_shared<Type>(simple(TypeKind::U8));break;
            case NirOp::MakeSlice:{t.kind=TypeKind::Slice;if(!in.args.empty()){auto p=regType(fn,in.args[0]);if(p.kind==TypeKind::Pointer&&p.pointee)t.element=p.pointee;}break;}
            case NirOp::SliceData:if(!in.args.empty()){auto s=regType(fn,in.args[0]);t.kind=TypeKind::Pointer;if(s.kind==TypeKind::Slice&&s.element)t.pointee=s.element;}break;
            case NirOp::SliceLen:t=simple(TypeKind::Usize);break;
            case NirOp::AtomicLoad:case NirOp::AtomicExchange:case NirOp::AtomicCompareExchange:if(!in.args.empty()){auto p=regType(fn,in.args[0]);if(p.kind==TypeKind::Pointer&&p.pointee)t=*p.pointee;}break;
            case NirOp::Intrinsic:if(in.text=="x86.rdtsc")t=simple(TypeKind::U64);else t=simple(TypeKind::Void);break;
            case NirOp::Try:if(!in.args.empty())t=regType(fn,in.args[0]);break;
            case NirOp::CheckNonNull:case NirOp::CheckBounds:case NirOp::AtomicStore:case NirOp::AtomicFence:case NirOp::InlineAsm:case NirOp::StoreMemory:case NirOp::Jump:case NirOp::JumpIfFalse:case NirOp::Return:case NirOp::Throw:case NirOp::Nop:t=simple(TypeKind::Void);break;
        }in.type=t;if(in.dest){ensureReg(fn,*in.dest);fn.registerTypes[*in.dest]=t;}}
        blocks(fn);
    };
    analyzeFunction(nir.entry);for(auto&fn:nir.functions)analyzeFunction(fn);
}
void NirAnalyzer::rebuildControlFlow(NirProgram&nir)const{blocks(nir.entry);for(auto&fn:nir.functions)blocks(fn);}
} // namespace noe
