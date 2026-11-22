#include "noe.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {

bool unsignedInteger(TypeKind k) {
    return k==TypeKind::U8||k==TypeKind::U16||k==TypeKind::U32||
           k==TypeKind::U64||k==TypeKind::Usize;
}
bool supportedScalar(TypeKind k) {
    Type t;t.kind=k;
    return k==TypeKind::Bool||k==TypeKind::Float||k==TypeKind::String||
           k==TypeKind::Pointer||
           (t.isInteger()&&t.size()>0&&t.size()<=8);
}
std::string reg(Reg r) { return "r_" + std::to_string(r); }
std::string args(const NirInstruction& in,std::size_t index) {
    return index<in.args.size()?reg(in.args[index]):"UINT64_C(0)";
}
std::string quote(const std::string& s) {
    std::ostringstream out;out << '"';
    for(unsigned char ch:s){
        if(ch=='"'||ch=='\\')out << '\\' << static_cast<char>(ch);
        else if(ch>=32&&ch<=126)out << static_cast<char>(ch);
        else {out << '\\' << char('0'+(ch>>6)) << char('0'+((ch>>3)&7)) << char('0'+(ch&7));}
    }
    out << '"';return out.str();
}
const char* runtime=R"C(
/* Portable C11 machine-code bridge. Generated from typed Noqeri NIR. */
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdalign.h>

#if defined(__GNUC__) || defined(__clang__)
#define NQ_COLD_NORETURN __attribute__((cold, noreturn))
#define NQ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NQ_COLD_NORETURN
#define NQ_UNLIKELY(x) (x)
#endif

static void* nq_abi_ctx = NULL;
static int32_t (*nq_abi_write)(void*, const char*, size_t) = NULL;

static inline uint64_t nq_bits(double x){uint64_t b=0;memcpy(&b,&x,sizeof b);return b;}
static inline double nq_double(uint64_t b){double x=0;memcpy(&x,&b,sizeof x);return x;}
NQ_COLD_NORETURN static void nq_trap(void){abort();}
NQ_COLD_NORETURN static void nq_bounds_trap(uint64_t index, uint64_t length){
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "NQR-R4000: runtime error: index out of bounds: index=%" PRIu64 ", length=%" PRIu64 "\n", index, length);
    if (nq_abi_write && n > 0) {
        nq_abi_write(nq_abi_ctx, buf, (size_t)n);
    } else if (n > 0) {
        fwrite(buf, 1, (size_t)n, stderr);
        fflush(stderr);
    }
    exit(1);
}
static inline uint64_t nq_narrow(uint64_t n,unsigned width,int is_signed){
    if(width>=64)return n;
    uint64_t mask=(UINT64_C(1)<<width)-UINT64_C(1);
    n &= mask;
    if(is_signed&&(n&(UINT64_C(1)<<(width-1))))n|=~mask;
    return n;
}
static inline uint64_t nq_from_float(uint64_t bits,unsigned width,int is_unsigned){
    double d=nq_double(bits);
    if(NQ_UNLIKELY(!isfinite(d)))nq_trap();
    double value=trunc(d);
    double upper=ldexp(1.0,is_unsigned?(int)width:(int)width-1);
    double lower=is_unsigned?0.0:-ldexp(1.0,(int)width-1);
    if(NQ_UNLIKELY(value<lower||value>=upper))nq_trap();
    if(is_unsigned)return nq_narrow((uint64_t)value,width,0);
    return nq_narrow((uint64_t)(int64_t)value,width,1);
}
static inline uint64_t nq_div(uint64_t a,uint64_t b,int is_unsigned,int remainder){
    if(NQ_UNLIKELY(b==0))nq_trap();
    if(is_unsigned)return remainder?a%b:a/b;
    int64_t x=(int64_t)a,y=(int64_t)b;
    if(NQ_UNLIKELY(x==INT64_MIN&&y==-1))nq_trap();
    return (uint64_t)(remainder?x%y:x/y);
}
static void nq_print(uint64_t bits,int kind){
    char buf[128];
    int len=0;
    switch(kind){
      case 0:len=snprintf(buf,sizeof(buf),"%" PRId64 "\n",(int64_t)bits);break;
      case 1:len=snprintf(buf,sizeof(buf),"%" PRIu64 "\n",bits);break;
      case 2:len=snprintf(buf,sizeof(buf),"%s\n",bits?"true":"false");break;
      case 3:len=snprintf(buf,sizeof(buf),"%.6f\n",nq_double(bits));break;
      case 4:if(!bits)nq_trap();len=snprintf(buf,sizeof(buf),"%s\n",(const char*)(uintptr_t)bits);break;
      default:nq_trap();
    }
    if(nq_abi_write&&len>0){
        nq_abi_write(nq_abi_ctx,buf,(size_t)len);
    }else if(len>0){
        fwrite(buf,1,(size_t)len,stdout);
    }
}
)C";

bool emitFunction(std::ostream& out,const NirFunction& fn,
                  const std::unordered_map<std::string,std::string>& symbols,
                  Diagnostics& diagnostics,const std::string& symbol) {
    if(fn.isExtern){
        diagnostics.error("NQR-C8201",{},"portable C backend cannot link external Noqeri function '"+fn.name+"'");
        return false;
    }
    if(fn.nextReg>1000000||fn.code.size()>1000000||fn.params.size()>64){
        diagnostics.error("NQR-C8202",{},"portable native function exceeds bounded register/instruction/argument limits");
        return false;
    }
    for(const auto& t:fn.paramTypes){
        if(!supportedScalar(t.kind)){
            diagnostics.error("NQR-C8209",{},"portable C11 backend does not support pointer or aggregate function arguments");
            return false;
        }
    }
    if(fn.resultType.kind!=TypeKind::Unknown &&
       fn.resultType.kind!=TypeKind::Void &&
       !supportedScalar(fn.resultType.kind)){
        diagnostics.error("NQR-C8209",{},"portable C11 backend does not support pointer or aggregate function returns");
        return false;
    }
    for(const auto& in:fn.code){
        const bool valid=[&](){
            switch(in.op){
            case NirOp::Const:
                return !std::holds_alternative<std::monostate>(in.literal);
            case NirOp::Load:case NirOp::Store:case NirOp::Nop:
                return true;
            case NirOp::Unary:case NirOp::Binary:{
                if(in.args.size()!=(in.op==NirOp::Unary?1u:2u))
                    return false;
                for(const auto r:in.args){
                    if(r>=fn.registerTypes.size())return false;
                    const auto kind=fn.registerTypes[r].kind;
                    if(!(fn.registerTypes[r].isNumeric()||
                         kind==TypeKind::Bool||kind==TypeKind::Pointer))return false;
                }
                if(in.op==NirOp::Unary)
                    return in.text=="!"||in.text=="-";
                return in.text=="+"||in.text=="-"||in.text=="*"||
                       in.text=="/"||in.text=="%"||
                       in.text=="=="||in.text=="!="||in.text=="<"||
                       in.text=="<="||in.text==">"||in.text==">="||
                       in.text=="&&"||in.text=="||";
            }
            case NirOp::Cast:{
                if(in.args.size()!=1)return false;
                Type src;
                if(in.args[0]<fn.registerTypes.size())src=fn.registerTypes[in.args[0]];
                return (src.isNumeric()||src.kind==TypeKind::Pointer)&&
                       (in.type.isNumeric()||in.type.kind==TypeKind::Pointer);
            }
            case NirOp::Call:{
                if(in.text=="print")
                    return in.args.size()==1;
                auto it=symbols.find(in.text);
                return it!=symbols.end()&&in.args.size()<=64;
            }
            case NirOp::Jump:case NirOp::JumpIfFalse:
                return in.target<=fn.code.size()&&
                       (in.op==NirOp::Jump||in.args.size()==1);
            case NirOp::Return:
                return in.args.size()<=1;
            case NirOp::StackAlloc:
                return in.dest.has_value();
            case NirOp::PtrOffset:
                return in.dest.has_value()&&in.args.size()==2;
            case NirOp::LoadMemory:
                return in.dest.has_value()&&in.args.size()==1;
            case NirOp::StoreMemory:
                return in.args.size()==2;
            case NirOp::CheckBounds:
                return in.args.size()==2;
            case NirOp::CheckNonNull:
                return in.args.size()==1;
            case NirOp::AddressOf:
                return in.dest.has_value();
            case NirOp::MakeSlice:
                return in.dest.has_value()&&in.args.size()==2;
            case NirOp::SliceData:case NirOp::SliceLen:
                return in.dest.has_value()&&in.args.size()==1;
            default:return false;
            }
        }();
        if(!valid){
            diagnostics.error("NQR-C8203",{},
                "portable C native backend rejects unsupported NIR operation in '"+fn.name+
                "' (no interpreter or unsafe fallback)");
            return false;
        }
        if(in.dest&&*in.dest>=fn.nextReg){
            diagnostics.error("NQR-C8204",{},"invalid destination register in portable native function '"+fn.name+"'");
            return false;
        }
        for(const auto r:in.args)if(r>=fn.nextReg){
            diagnostics.error("NQR-C8204",{},"invalid operand register in portable native function '"+fn.name+"'");
            return false;
        }
    }
    std::map<std::string,std::size_t> vars;
    for(const auto& p:fn.params)vars.emplace(p,vars.size());
    for(const auto& in:fn.code)
        if(in.op==NirOp::Load||in.op==NirOp::Store||in.op==NirOp::AddressOf)
            if(!vars.count(in.text))vars.emplace(in.text,vars.size());

    // Scalar non-negative analysis so signed non-negative '/' and '%' lower
    // cleanly to unsigned hardware division without negative-dividend fixup.
    std::vector<unsigned> definitions(fn.nextReg,0);
    std::unordered_map<Reg,std::int64_t> constantValues;
    for(const auto& in:fn.code)
        if(in.dest&&*in.dest<definitions.size())++definitions[*in.dest];
    for(const auto& in:fn.code){
        if(!in.dest||definitions[*in.dest]!=1)continue;
        if(in.op==NirOp::Const){
            if(auto v=std::get_if<std::int64_t>(&in.literal))constantValues[*in.dest]=*v;
        }else if(in.op==NirOp::Cast&&in.args.size()==1&&
                 (in.text=="int"||in.text=="i64"||in.text=="u64"||in.text=="usize"||in.text=="isize")){
            if(auto it=constantValues.find(in.args[0]);it!=constantValues.end())
                constantValues[*in.dest]=it->second;
        }
    }
    std::vector<bool> nonNegative(fn.nextReg,false);
    std::unordered_map<std::string,bool> nonNegativeVars;
    for(std::size_t r=0;r<fn.registerTypes.size()&&r<fn.nextReg;++r)
        if(unsignedInteger(fn.registerTypes[r].kind))nonNegative[r]=true;
    for(const auto& [r,val]:constantValues)
        if(val>=0&&r<nonNegative.size())nonNegative[r]=true;
    for(int pass=0;pass<4;++pass){
        bool updated=false;
        for(const auto& in:fn.code){
            if(in.op==NirOp::Store&&!in.args.empty()&&in.args[0]<nonNegative.size()){
                auto it=nonNegativeVars.find(in.text);
                if(it==nonNegativeVars.end())nonNegativeVars[in.text]=nonNegative[in.args[0]];
                else if(!nonNegative[in.args[0]])it->second=false;
            }
            if(!in.dest||*in.dest>=nonNegative.size()||nonNegative[*in.dest])continue;
            bool isNonNeg=false;
            if(in.op==NirOp::Const){
                if(auto v=std::get_if<std::int64_t>(&in.literal))isNonNeg=(*v>=0);
                else if(std::holds_alternative<bool>(in.literal))isNonNeg=true;
            }else if(in.op==NirOp::Load){
                auto it=nonNegativeVars.find(in.text);
                if(it!=nonNegativeVars.end()&&it->second)isNonNeg=true;
            }else if(in.op==NirOp::Cast&&in.args.size()==1){
                if(unsignedInteger(in.type.kind)||
                   (in.args[0]<nonNegative.size()&&nonNegative[in.args[0]]))
                    isNonNeg=true;
            }else if(in.op==NirOp::Binary&&in.args.size()==2){
                auto isNN=[&](Reg r){
                    return (r<nonNegative.size()&&nonNegative[r])||
                           (constantValues.count(r)&&constantValues.at(r)>=0);
                };
                if(in.text=="=="||in.text=="!="||in.text=="<"||in.text=="<="||
                   in.text==">"||in.text==">="||in.text=="&&"||in.text=="||")isNonNeg=true;
                else if((in.text=="+"||in.text=="*")&&isNN(in.args[0])&&isNN(in.args[1]))isNonNeg=true;
                else if(in.text=="%"&&isNN(in.args[0]))isNonNeg=true;
                else if(in.text=="/"&&isNN(in.args[0])&&constantValues.count(in.args[1])&&constantValues.at(in.args[1])>0)isNonNeg=true;
            }
            if(isNonNeg){nonNegative[*in.dest]=true;updated=true;}
        }
        if(!updated)break;
    }

    // Track non-escaping StackAlloc arrays so LLVM/GCC see disjoint C arrays
    // m_<pc>[idx] without (uintptr_t) pointer-aliasing escapes.
    const std::size_t noAlloc=fn.code.size();
    std::vector<std::size_t> allocRoot(fn.nextReg,noAlloc);
    struct BindingInfo{std::size_t stores=0;std::size_t pc=0;Reg src=0;bool taken=false;};
    std::unordered_map<std::string,BindingInfo> varBindings;
    std::vector<bool> prefixBranch(fn.code.size()+1,false);
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        prefixBranch[pc+1]=prefixBranch[pc]||in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse||
            in.op==NirOp::Return||in.op==NirOp::Throw;
        if(in.op==NirOp::StackAlloc&&in.dest&&definitions[*in.dest]==1&&(in.width%8==0||in.width>=8))
            allocRoot[*in.dest]=pc;
        if(in.op==NirOp::Store){
            auto& b=varBindings[in.text];
            ++b.stores;b.pc=pc;
            if(in.args.size()==1)b.src=in.args[0]; else b.stores=2;
        }
        if(in.op==NirOp::AddressOf)varBindings[in.text].taken=true;
    }
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        if(in.op==NirOp::Load&&in.dest&&definitions[*in.dest]==1&&!in.isVolatile){
            auto it=varBindings.find(in.text);
            if(it!=varBindings.end()&&it->second.stores==1&&!it->second.taken&&
               !prefixBranch[it->second.pc]&&pc>it->second.pc&&
               it->second.src<allocRoot.size()&&allocRoot[it->second.src]!=noAlloc){
                allocRoot[*in.dest]=allocRoot[it->second.src];
            }
        }
    }
    std::unordered_set<std::size_t> directAllocs;
    for(std::size_t pc=0;pc<fn.code.size();++pc)
        if(fn.code[pc].op==NirOp::StackAlloc&&fn.code[pc].dest&&allocRoot[*fn.code[pc].dest]==pc)
            directAllocs.insert(pc);

    std::unordered_map<Reg,std::pair<std::size_t,Reg>> directPtrs;
    std::vector<bool> skipDirectBaseLoad(fn.code.size(),false);
    // Validate that all uses of a directAlloc are strictly PtrOffset(base,idx)*8
    // whose outputs only feed width-8 LoadMemory/StoreMemory with single-def idx.
    std::unordered_set<std::size_t> nonDirectAllocs;
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        for(std::size_t k=0;k<in.args.size();++k){
            const Reg r=in.args[k];
            if(r>=allocRoot.size()||allocRoot[r]==noAlloc)continue;
            const std::size_t root=allocRoot[r];
            if(in.op==NirOp::Store&&k==0){
                auto it=varBindings.find(in.text);
                if(it!=varBindings.end()&&it->second.stores==1&&!it->second.taken&&!prefixBranch[it->second.pc])
                    continue;
            }
            if(in.op==NirOp::PtrOffset&&k==0&&in.width==8&&in.args.size()==2&&in.dest&&
               definitions[*in.dest]==1&&in.args[1]<definitions.size()&&definitions[in.args[1]]==1){
                directPtrs[*in.dest]=std::make_pair(root,in.args[1]);
                continue;
            }
            nonDirectAllocs.insert(root);
        }
    }
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        for(std::size_t k=0;k<in.args.size();++k){
            auto it=directPtrs.find(in.args[k]);
            if(it==directPtrs.end())continue;
            if(!((in.op==NirOp::LoadMemory&&k==0&&in.width==8)||
                 (in.op==NirOp::StoreMemory&&k==0&&in.width==8)))
                nonDirectAllocs.insert(it->second.first);
        }
    }
    for(std::size_t bad:nonDirectAllocs)directAllocs.erase(bad);
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        if(in.op==NirOp::Store&&!in.args.empty()&&in.args[0]<allocRoot.size()&&
           directAllocs.count(allocRoot[in.args[0]]))
            skipDirectBaseLoad[pc]=true;
        if(in.op==NirOp::Load&&in.dest&&*in.dest<allocRoot.size()&&
           directAllocs.count(allocRoot[*in.dest]))
            skipDirectBaseLoad[pc]=true;
    }

    std::vector<bool> regUsed(fn.nextReg,false);
    for(const auto& in:fn.code){
        if(in.dest&&*in.dest<regUsed.size())regUsed[*in.dest]=true;
        for(Reg a:in.args)if(a<regUsed.size())regUsed[a]=true;
    }

    out<<"static uint64_t "<<symbol<<"(";
    if(fn.params.empty())out<<"void";
    for(std::size_t i=0;i<fn.params.size();++i){
        if(i)out<<", ";
        out<<"uint64_t p"<<i;
    }
    out<<"){\n uint64_t r_dummy=0;\n (void)r_dummy;\n";
    for(std::uint32_t r=0;r<fn.nextReg;++r)
        if(regUsed[r])out<<" uint64_t r_"<<r<<"=0;\n";
    for(std::size_t v=0;v<vars.size();++v)
        out<<" uint64_t v_"<<v<<"=0;\n";
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        if(in.op==NirOp::StackAlloc){
            out<<" _Alignas(64) uint64_t m_"<<pc<<"["<<std::max<std::size_t>((in.width+7)/8,1)<<"];\n";
            if(!directAllocs.count(pc)||in.width<=4096)
                out<<" memset(m_"<<pc<<", 0, sizeof(m_"<<pc<<"));\n";
        }else if(in.op==NirOp::MakeSlice){
            out<<" uint64_t s_"<<pc<<"[2]={0};\n";
        }
    }
    for(std::size_t i=0;i<fn.params.size();++i)out<<" v_"<<vars.at(fn.params[i])<<"=p"<<i<<";\n";
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];const auto dst=in.dest?reg(*in.dest):"r_dummy";
        const TypeKind srcKind=!in.args.empty()&&in.args[0]<fn.registerTypes.size()?
            fn.registerTypes[in.args[0]].kind:TypeKind::Unknown;
        const auto lhs=args(in,0),rhs=args(in,1);
        out<<"L"<<pc<<":;\n";
        switch(in.op){
        case NirOp::Const:
            if(in.dest&&*in.dest<fn.registerTypes.size()&&fn.registerTypes[*in.dest].kind==TypeKind::Float){
                if(auto v=std::get_if<double>(&in.literal))
                    out<<" "<<dst<<"=nq_bits("<<std::setprecision(17)<<*v<<");\n";
                else if(auto v=std::get_if<std::int64_t>(&in.literal))
                    out<<" "<<dst<<"=nq_bits((double)"<<*v<<");\n";
                else if(auto v=std::get_if<bool>(&in.literal))
                    out<<" "<<dst<<"=nq_bits("<<(*v?"1.0":"0.0")<<");\n";
            }else{
                if(auto v=std::get_if<std::int64_t>(&in.literal))
                    out<<" "<<dst<<"=UINT64_C("<<static_cast<std::uint64_t>(*v)<<");\n";
                else if(auto v=std::get_if<bool>(&in.literal))
                    out<<" "<<dst<<"=UINT64_C("<<(*v?1:0)<<");\n";
                else if(auto v=std::get_if<double>(&in.literal))
                    out<<" "<<dst<<"=nq_bits("<<std::setprecision(17)<<*v<<");\n";
                else if(auto v=std::get_if<std::string>(&in.literal)){
                    if(v->find('\0')!=std::string::npos){
                        diagnostics.error("NQR-C8205",{},"portable native strings cannot contain embedded NUL bytes");return false;
                    }
                    out<<" "<<dst<<"=(uint64_t)(uintptr_t)"<<quote(*v)<<";\n";
                }
            }
            break;
        case NirOp::Store:
            if(skipDirectBaseLoad[pc])out<<" (void)0;\n";
            else out<<" v_"<<vars.at(in.text)<<"="<<lhs<<";\n";
            break;
        case NirOp::Load:
            if(skipDirectBaseLoad[pc])out<<" (void)0;\n";
            else out<<" "<<dst<<"=v_"<<vars.at(in.text)<<";\n";
            break;
        case NirOp::Unary:
            if(in.text=="!")out<<" "<<dst<<"=!"<<lhs<<";\n";
            else if(srcKind==TypeKind::Float)out<<" "<<dst<<"=nq_bits(-nq_double("<<lhs<<"));\n";
            else out<<" "<<dst<<"=UINT64_C(0)-"<<lhs<<";\n";
            break;
        case NirOp::Binary:{
            const bool floating=srcKind==TypeKind::Float;
            const bool isUnsigned=unsignedInteger(srcKind);
            if(in.text=="&&"||in.text=="||"){
                out<<" "<<dst<<"=(("<<lhs<<"!=0)"<<in.text<<"("<<rhs<<"!=0));\n";
                break;
            }
            const bool comp=in.text=="=="||in.text=="!="||in.text=="<"||
                            in.text=="<="||in.text==">"||in.text==">=";
            if(floating){
                if(in.text=="%"){diagnostics.error("NQR-C8203",{},"portable C backend does not support float remainder");return false;}
                out<<" "<<dst<<"=";
                if(!comp)out<<"nq_bits(";
                out<<"(nq_double("<<lhs<<") "<<in.text<<" nq_double("<<rhs<<"))";
                if(!comp)out<<")";
                out<<";\n";
            }else if(in.text=="/"||in.text=="%"){
                const bool nonNegDiv = isUnsigned ||
                    (in.args[0]<nonNegative.size()&&nonNegative[in.args[0]]&&
                     ((in.args[1]<nonNegative.size()&&nonNegative[in.args[1]])||
                      (constantValues.count(in.args[1])&&constantValues.at(in.args[1])>0)));
                out<<" "<<dst<<"=nq_div("<<lhs<<","<<rhs<<","<<(nonNegDiv?1:0)<<","<<(in.text=="%"?1:0)<<");\n";
            }else{
                const std::string cast=isUnsigned?"(uint64_t)":"(int64_t)";
                out<<" "<<dst<<"=";
                if(comp)out<<"("<<cast<<lhs<<" "<<in.text<<" "<<cast<<rhs<<")";
                else out<<"("<<lhs<<" "<<in.text<<" "<<rhs<<")";
                out<<";\n";
            }
            break;
        }
        case NirOp::Cast:{
            const TypeKind target=in.type.kind;
            if(target==TypeKind::Float){
                if(srcKind==TypeKind::Float)out<<" "<<dst<<"="<<lhs<<";\n";
                else out<<" "<<dst<<"=nq_bits((double)("<<(unsignedInteger(srcKind)?"uint64_t":"int64_t")<<")"<<lhs<<");\n";
            }else if(srcKind==TypeKind::Float){
                out<<" "<<dst<<"=nq_from_float("<<lhs<<","<<(in.type.size()*8)<<","<<(unsignedInteger(target)?1:0)<<");\n";
            }else if(target==TypeKind::Pointer||srcKind==TypeKind::Pointer||
                     (in.type.size()==8 && srcKind==target)){
                out<<" "<<dst<<"="<<lhs<<";\n";
            }else{
                out<<" "<<dst<<"=nq_narrow("<<lhs<<","<<(in.type.size()*8)<<","<<(unsignedInteger(target)?0:1)<<");\n";
            }
            break;
        }
        case NirOp::Call:
            if(in.text=="print"){
                int type=-1;
                if(supportedScalar(srcKind)){
                    if(srcKind==TypeKind::String)type=4;
                    else if(srcKind==TypeKind::Bool)type=2;
                    else if(srcKind==TypeKind::Float)type=3;
                    else if(unsignedInteger(srcKind))type=0;
                    else type=0;
                }
                if(type<0){diagnostics.error("NQR-C8206",{},"portable C print supports scalar int/bool/float/string only");return false;}
                out<<" nq_print("<<lhs<<","<<type<<");\n";
                if(in.dest)out<<" "<<dst<<"=UINT64_C(0);\n";
            }else{
                out<<" "<<dst<<"="<<symbols.at(in.text)<<"(";
                for(std::size_t i=0;i<in.args.size();++i){if(i)out<<",";out<<args(in,i);}
                out<<");\n";
            }
            break;
        case NirOp::Jump:out<<" goto L"<<in.target<<";\n";break;
        case NirOp::JumpIfFalse:out<<" if(!"<<lhs<<")goto L"<<in.target<<";\n";break;
        case NirOp::Return:out<<" return "<<(in.args.empty()?"UINT64_C(0)":lhs)<<";\n";break;
        case NirOp::Nop:out<<" (void)0;\n";break;
        case NirOp::StackAlloc:
            if(directAllocs.count(pc))out<<" (void)0;\n";
            else out<<" "<<dst<<"=(uint64_t)(uintptr_t)m_"<<pc<<";\n";
            break;
        case NirOp::PtrOffset:
            if(in.dest&&directPtrs.count(*in.dest)&&directAllocs.count(directPtrs.at(*in.dest).first))
                out<<" (void)0;\n";
            else
                out<<" "<<dst<<"=(uint64_t)(uintptr_t)((char*)(uintptr_t)"<<lhs<<"+((int64_t)"<<rhs<<"*"<<in.width<<"));\n";
            break;
        case NirOp::LoadMemory:
            if(in.width==8&&!in.args.empty()){
                auto dp=directPtrs.find(in.args[0]);
                if(dp!=directPtrs.end()&&directAllocs.count(dp->second.first)){
                    out<<" "<<dst<<"=m_"<<dp->second.first<<"[(uint64_t)"<<reg(dp->second.second)<<"];\n";
                    break;
                }
            }
            if(in.width==1)out<<" "<<dst<<"=*(const uint8_t*)(uintptr_t)"<<lhs<<";\n";
            else if(in.width==2)out<<" "<<dst<<"=*(const uint16_t*)(uintptr_t)"<<lhs<<";\n";
            else if(in.width==4)out<<" "<<dst<<"=*(const uint32_t*)(uintptr_t)"<<lhs<<";\n";
            else out<<" "<<dst<<"=*(const uint64_t*)(uintptr_t)"<<lhs<<";\n";
            break;
        case NirOp::StoreMemory:
            if(in.width==8&&in.args.size()==2){
                auto dp=directPtrs.find(in.args[0]);
                if(dp!=directPtrs.end()&&directAllocs.count(dp->second.first)){
                    out<<" m_"<<dp->second.first<<"[(uint64_t)"<<reg(dp->second.second)<<"]="<<rhs<<";\n";
                    break;
                }
            }
            if(in.width==1)out<<" *(uint8_t*)(uintptr_t)"<<lhs<<"=(uint8_t)"<<rhs<<";\n";
            else if(in.width==2)out<<" *(uint16_t*)(uintptr_t)"<<lhs<<"=(uint16_t)"<<rhs<<";\n";
            else if(in.width==4)out<<" *(uint32_t*)(uintptr_t)"<<lhs<<"=(uint32_t)"<<rhs<<";\n";
            else out<<" *(uint64_t*)(uintptr_t)"<<lhs<<"="<<rhs<<";\n";
            break;
        case NirOp::CheckBounds:
            out<<" if(NQ_UNLIKELY((uint64_t)"<<lhs<<">=(uint64_t)"<<rhs<<"))nq_bounds_trap((uint64_t)"<<lhs<<",(uint64_t)"<<rhs<<");\n";
            break;
        case NirOp::CheckNonNull:
            out<<" if(NQ_UNLIKELY(!"<<lhs<<"))nq_trap();\n";
            break;
        case NirOp::AddressOf:
            out<<" "<<dst<<"=(uint64_t)(uintptr_t)&v_"<<vars.at(in.text)<<";\n";
            break;
        case NirOp::MakeSlice:
            out<<" s_"<<pc<<"[0]="<<lhs<<"; s_"<<pc<<"[1]="<<rhs<<";\n";
            out<<" "<<dst<<"=(uint64_t)(uintptr_t)s_"<<pc<<";\n";
            break;
        case NirOp::SliceData:
            out<<" "<<dst<<"=*(const uint64_t*)(uintptr_t)"<<lhs<<";\n";
            break;
        case NirOp::SliceLen:
            out<<" "<<dst<<"=*(const uint64_t*)((uintptr_t)"<<lhs<<"+8);\n";
            break;
        default:break;
        }
    }
    out<<"L"<<fn.code.size()<<":;\n return UINT64_C(0);\n}\n";
    return true;
}
} // namespace

bool PortableCBackend::emitSource(const NirProgram& program,
    const std::filesystem::path& output,Diagnostics& diagnostics) const{
    std::unordered_map<std::string,std::string> symbols;
    for(std::size_t i=0;i<program.functions.size();++i){
        if(!symbols.emplace(program.functions[i].name,"nq_fn_"+std::to_string(i)).second){
            diagnostics.error("NQR-C8207",{},"duplicate function symbol in portable native output");return false;
        }
    }
    std::ostringstream out;
    out<<runtime<<"\n";
    for(const auto& fn:program.functions){
        out<<"static uint64_t "<<symbols.at(fn.name)<<"(";
        if(fn.params.empty())out<<"void";
        for(std::size_t i=0;i<fn.params.size();++i){
            if(i)out<<", ";
            out<<"uint64_t";
        }
        out<<");\n";
    }
    out<<"static uint64_t nq_entry(void);\n";
    for(const auto& fn:program.functions)
        if(!emitFunction(out,fn,symbols,diagnostics,symbols.at(fn.name)))return false;
    if(!emitFunction(out,program.entry,symbols,diagnostics,"nq_entry"))return false;
    out<<"typedef struct nq_abi_view { uint32_t v; uint32_t s; void* ctx; int32_t (*w)(void*,const char*,size_t); } nq_abi_view;\n"
       <<"int64_t noqeri_entry(void* abi_ptr){\n"
       <<"    if(abi_ptr){\n"
       <<"        nq_abi_view* a = (nq_abi_view*)abi_ptr;\n"
       <<"        nq_abi_ctx=a->ctx;\n"
       <<"        nq_abi_write=a->w;\n"
       <<"    }\n"
       <<"    return (int64_t)nq_entry();\n"
       <<"}\n"
       <<"#ifndef NOQERI_NO_MAIN\n"
       <<"int main(void){return (int)noqeri_entry(NULL);}\n"
       <<"#endif\n";
    auto parent=output.parent_path();if(parent.empty())parent=".";
    std::error_code ec;std::filesystem::create_directories(parent,ec);
    if(ec){diagnostics.error("NQR-C8208",{},"cannot create portable C output directory: "+ec.message());return false;}
    std::ofstream file(output,std::ios::binary|std::ios::trunc);
    if(!file){diagnostics.error("NQR-C8208",{},"could not open portable C output file");return false;}
    file<<out.str();
    if(!file.good()){diagnostics.error("NQR-C8208",{},"failed to write portable C source");return false;}
    return true;
}
} // namespace noe
