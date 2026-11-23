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
           k==TypeKind::Pointer||k==TypeKind::Slice||
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
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
#define NQ_COLD_NORETURN __attribute__((cold, noreturn))
#define NQ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NQ_COLD_NORETURN
#define NQ_UNLIKELY(x) (x)
#endif

static void* nq_abi_ctx = NULL;
static int32_t (*nq_abi_write)(void*, const char*, size_t) = NULL;

static inline uint64_t nq_clock_millis(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;
    return (uint64_t)ts.tv_sec*UINT64_C(1000)+(uint64_t)(ts.tv_nsec/1000000L);
}
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
                return !std::holds_alternative<std::monostate>(in.literal)||in.type.kind==TypeKind::Null;
            case NirOp::Load:case NirOp::Store:case NirOp::Nop:
                return true;
            case NirOp::Unary:case NirOp::Binary:{
                if(in.args.size()!=(in.op==NirOp::Unary?1u:2u))
                    return false;
                for(const auto r:in.args){
                    if(r>=fn.registerTypes.size())return false;
                    const auto kind=fn.registerTypes[r].kind;
                    if(!(fn.registerTypes[r].isNumeric()||
                         kind==TypeKind::Bool||kind==TypeKind::Pointer||kind==TypeKind::Null))return false;
                }
                if(in.op==NirOp::Unary)
                    return in.text=="!"||in.text=="-"||in.text=="~";
                return in.text=="+"||in.text=="-"||in.text=="*"||
                       in.text=="/"||in.text=="%"||
                       in.text=="&"||in.text=="|"||in.text=="^"||
                       in.text=="<<"||in.text==">>"||
                       in.text=="=="||in.text=="!="||in.text=="<"||
                       in.text=="<="||in.text==">"||in.text==">="||
                       in.text=="&&"||in.text=="||";
            }
            case NirOp::Cast:{
                if(in.args.size()!=1)return false;
                Type src;
                if(in.args[0]<fn.registerTypes.size())src=fn.registerTypes[in.args[0]];
                return (src.isNumeric()||src.kind==TypeKind::Bool||src.kind==TypeKind::Pointer)&&
                       (in.type.isNumeric()||in.type.kind==TypeKind::Bool||in.type.kind==TypeKind::Pointer);
            }
            case NirOp::Call:{
                if(in.text=="print"||in.text=="textLength")
                    return in.args.size()==1;
                if(in.text=="clockMillis")
                    return in.args.empty();
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
                return in.dest.has_value()&&(in.args.size()==2||in.args.size()==1);
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
            case NirOp::AtomicLoad:
                return in.dest.has_value()&&in.args.size()==1&&(in.width==1||in.width==2||in.width==4||in.width==8);
            case NirOp::AtomicStore:
                return in.args.size()==2&&(in.width==1||in.width==2||in.width==4||in.width==8);
            case NirOp::AtomicExchange:
                return in.dest.has_value()&&in.args.size()==2&&(in.width==1||in.width==2||in.width==4||in.width==8);
            case NirOp::AtomicCompareExchange:
                return in.dest.has_value()&&in.args.size()==3&&(in.width==1||in.width==2||in.width==4||in.width==8);
            case NirOp::AtomicFence:
                return true;
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

    // Track upper bounds for non-negative scalars and induction variables so
    // proven uint32-range expressions lower cleanly to 32-bit AVX2 div/mod/mul.
    std::vector<bool> jumpTargets(fn.code.size()+1,false);
    for(const auto& in:fn.code){
        if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&in.target<=fn.code.size())
            jumpTargets[in.target]=true;
    }
    std::vector<std::uint64_t> maxVal(fn.nextReg,std::numeric_limits<std::uint64_t>::max());
    std::unordered_map<std::string,std::uint64_t> varMaxVal;
    std::unordered_map<std::string,std::vector<std::size_t>> varStorePcs;
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        if(fn.code[pc].op==NirOp::Store)varStorePcs[fn.code[pc].text].push_back(pc);
    }
    for(std::size_t r=0;r<fn.nextReg;++r){
        if(auto it=constantValues.find(static_cast<Reg>(r));it!=constantValues.end()&&it->second>=0)
            maxVal[r]=static_cast<std::uint64_t>(it->second);
        else if(r<fn.registerTypes.size()){
            const auto k=fn.registerTypes[r].kind;
            if(k==TypeKind::U8)maxVal[r]=255ULL;
            else if(k==TypeKind::U16)maxVal[r]=65535ULL;
            else if(k==TypeKind::U32)maxVal[r]=4294967295ULL;
            else if(k==TypeKind::Bool)maxVal[r]=1ULL;
        }
    }
    // Deduce induction variable upper bounds: init >= 0 && all increments v = v + step
    // are immediately guarded in the same loop body by v < B or v <= B.
    for(const auto& [vName,spcs]:varStorePcs){
        if(spcs.empty())continue;
        std::uint64_t bound=0;
        bool ok=true;
        for(std::size_t spc:spcs){
            const auto& st=fn.code[spc];
            if(st.isVolatile||st.args.size()!=1){ok=false;break;}
            const Reg val=st.args[0];
            if(auto it=constantValues.find(val);it!=constantValues.end()&&it->second>=0){
                bound=std::max(bound,static_cast<std::uint64_t>(it->second));
                continue;
            }
            // Check single-def Load(other) that already has varMaxVal (skip) or v + step guarded by < B
            std::int64_t step=0;
            bool isSelfInc=false;
            for(std::size_t dpc=0;dpc<spc;++dpc){
                const auto& di=fn.code[dpc];
                if(di.dest&&*di.dest==val&&di.op==NirOp::Binary&&di.text=="+"&&di.args.size()==2){
                    auto cR=constantValues.find(di.args[1]);
                    if(cR!=constantValues.end()&&cR->second>0&&cR->second<=1024){
                        for(std::size_t lpc=0;lpc<=dpc;++lpc){
                            if(fn.code[lpc].dest&&*fn.code[lpc].dest==di.args[0]&&
                               fn.code[lpc].op==NirOp::Load&&fn.code[lpc].text==vName){
                                isSelfInc=true;step=cR->second;break;
                            }
                        }
                    }
                }
            }
            if(!isSelfInc){ok=false;break;}
            std::uint64_t guardMax=std::numeric_limits<std::uint64_t>::max();
            for(std::size_t gpc=0;gpc<spc;++gpc){
                const auto& br=fn.code[gpc];
                if(br.op!=NirOp::JumpIfFalse||br.args.size()!=1||br.target<=spc)continue;
                for(std::size_t cpc=0;cpc<gpc;++cpc){
                    const auto& cmp=fn.code[cpc];
                    if(cmp.dest&&*cmp.dest==br.args[0]&&cmp.op==NirOp::Binary&&
                       (cmp.text=="<"||cmp.text=="<=")&&cmp.args.size()==2){
                        auto up=constantValues.find(cmp.args[1]);
                        if(up==constantValues.end()||up->second<=0)continue;
                        bool matchesVar=false;
                        for(std::size_t lpc=0;lpc<cpc;++lpc){
                            if(fn.code[lpc].dest&&*fn.code[lpc].dest==cmp.args[0]&&
                               fn.code[lpc].op==NirOp::Load&&fn.code[lpc].text==vName){
                                // Ensure no store to vName between lpc and spc other than nothing
                                bool written=false;
                                for(std::size_t xpc:spcs)if(xpc>lpc&&xpc<spc){written=true;break;}
                                // Also ensure no jump target skips the JumpIfFalse gpc into (gpc, spc]
                                bool jumpedIn=false;
                                for(std::size_t jpc=gpc+1;jpc<=spc;++jpc)if(jumpTargets[jpc]){jumpedIn=true;break;}
                                if(!written&&!jumpedIn){matchesVar=true;break;}
                            }
                        }
                        if(matchesVar){
                            const std::uint64_t incBound=static_cast<std::uint64_t>(up->second)+static_cast<std::uint64_t>(step);
                            guardMax=std::min(guardMax,incBound);
                        }
                    }
                }
            }
            if(guardMax==std::numeric_limits<std::uint64_t>::max()){ok=false;break;}
            bound=std::max(bound,guardMax);
        }
        if(ok)varMaxVal[vName]=bound;
    }
    std::vector<bool> localNonNeg=nonNegative;
    localNonNeg.resize(fn.nextReg,false);
    {
        std::vector<std::vector<std::size_t>> regDefPcs(fn.nextReg);
        for(std::size_t pc=0;pc<fn.code.size();++pc){
            if(fn.code[pc].dest&&*fn.code[pc].dest<regDefPcs.size())
                regDefPcs[*fn.code[pc].dest].push_back(pc);
        }
        auto unwrapCast=[&](Reg r)->Reg{
            for(int hop=0;hop<4;++hop){
                if(r>=regDefPcs.size()||regDefPcs[r].size()!=1)break;
                const auto& ci=fn.code[regDefPcs[r][0]];
                if(ci.op==NirOp::Cast&&ci.args.size()==1)r=ci.args[0];
                else break;
            }
            return r;
        };
        for(std::size_t backPc=0;backPc<fn.code.size();++backPc){
            const auto& back=fn.code[backPc];
            if(back.op!=NirOp::Jump||back.target>=backPc)continue;
            const std::size_t hdr=back.target;
            std::size_t brPc=hdr;
            for(std::size_t k=hdr;k<backPc;++k){
                if(fn.code[k].op==NirOp::JumpIfFalse&&fn.code[k].target>backPc){brPc=k;break;}
                if(fn.code[k].op==NirOp::Jump||fn.code[k].op==NirOp::JumpIfFalse)break;
            }
            if(brPc<=hdr||fn.code[brPc].args.size()!=1)continue;
            const Reg condR=fn.code[brPc].args[0];
            for(std::size_t cpc=hdr;cpc<brPc;++cpc){
                const auto& cmp=fn.code[cpc];
                if(!cmp.dest||*cmp.dest!=condR||cmp.op!=NirOp::Binary||
                   (cmp.text!="<"&&cmp.text!="<=")||cmp.args.size()!=2)continue;
                auto limIt=constantValues.find(cmp.args[1]);
                if(limIt==constantValues.end()||limIt->second<=0)continue;
                const Reg idxR=cmp.args[0];
                if(idxR>=regDefPcs.size()||regDefPcs[idxR].size()!=2)continue;
                const std::size_t d0=regDefPcs[idxR][0],d1=regDefPcs[idxR][1];
                if(d0>=hdr||d1<=brPc||d1>=backPc)continue;
                bool jumpedIn=false;
                for(std::size_t src=0;src<fn.code.size();++src){
                    if(src>brPc&&src<=backPc)continue;
                    const auto& jin=fn.code[src];
                    if((jin.op==NirOp::Jump||jin.op==NirOp::JumpIfFalse)&&
                       jin.target>brPc&&jin.target<=d1){jumpedIn=true;break;}
                }
                if(jumpedIn)continue;
                const auto& initIn=fn.code[d0];
                std::int64_t initVal=-1;
                if(auto cv=constantValues.find(idxR);cv!=constantValues.end())initVal=cv->second;
                else if(initIn.op==NirOp::Const){
                    if(auto v=std::get_if<std::int64_t>(&initIn.literal))initVal=*v;
                }else if(initIn.op==NirOp::Cast&&initIn.args.size()==1){
                    if(auto cv=constantValues.find(initIn.args[0]);cv!=constantValues.end())initVal=cv->second;
                }
                if(initVal<0||initVal>limIt->second)continue;
                const auto& stepIn=fn.code[d1];
                if(stepIn.op!=NirOp::Binary||stepIn.text!="+"||stepIn.args.size()!=2)continue;
                auto stepIt=constantValues.find(stepIn.args[1]);
                if(stepIt==constantValues.end()||stepIt->second<=0||stepIt->second>1024)continue;
                if(unwrapCast(stepIn.args[0])!=idxR)continue;
                const std::uint64_t bound=static_cast<std::uint64_t>(limIt->second)+static_cast<std::uint64_t>(stepIt->second);
                if(idxR<maxVal.size()){
                    maxVal[idxR]=std::min(maxVal[idxR],bound);
                    localNonNeg[idxR]=true;
                }
            }
        }
    }
    for(int pass=0;pass<6;++pass){
        bool upd=false;
        for(const auto& in:fn.code){
            if(!in.dest||*in.dest>=maxVal.size())continue;
            const Reg d=*in.dest;
            std::uint64_t cand=maxVal[d];
            bool candNN=localNonNeg[d];
            if(in.op==NirOp::Load&&!in.isVolatile){
                if(auto it=varMaxVal.find(in.text);it!=varMaxVal.end()){
                    cand=std::min(cand,it->second);
                    candNN=true;
                }
            }else if(in.op==NirOp::Cast&&in.args.size()==1){
                const Reg s=in.args[0];
                if(unsignedInteger(in.type.kind)&&in.type.size()<8){
                    const std::uint64_t cap=(in.type.size()==1)?255ULL:((in.type.size()==2)?65535ULL:4294967295ULL);
                    cand=std::min(cand,s<maxVal.size()?std::min(maxVal[s],cap):cap);
                    candNN=true;
                }else if(s<localNonNeg.size()&&localNonNeg[s]&&s<maxVal.size()&&
                         in.type.size()==8){
                    cand=std::min(cand,maxVal[s]);
                    candNN=true;
                }
            }else if(in.op==NirOp::Binary&&in.args.size()==2){
                const Reg a=in.args[0],b=in.args[1];
                const std::uint64_t ma=a<maxVal.size()?maxVal[a]:std::numeric_limits<std::uint64_t>::max();
                const std::uint64_t mb=b<maxVal.size()?maxVal[b]:std::numeric_limits<std::uint64_t>::max();
                const bool nna=(a<localNonNeg.size()&&localNonNeg[a]);
                const bool nnb=(b<localNonNeg.size()&&localNonNeg[b]);
                if(nna&&nnb){
                    if(in.text=="+"){
                        candNN=true;
                        if(ma<=std::numeric_limits<std::uint64_t>::max()-mb)
                            cand=std::min(cand,ma+mb);
                    }else if(in.text=="*"){
                        candNN=true;
                        if(ma==0||mb<=std::numeric_limits<std::uint64_t>::max()/ma)
                            cand=std::min(cand,ma*mb);
                    }else if(in.text=="%"&&mb>0&&mb<std::numeric_limits<std::uint64_t>::max()){
                        candNN=true;
                        cand=std::min(cand,mb-1);
                    }else if(in.text=="/"&&mb>=1){
                        candNN=true;
                        cand=std::min(cand,ma);
                    }else if(in.text=="&"){
                        candNN=true;
                        cand=std::min(cand,std::min(ma,mb));
                    }
                }
            }
            if(cand<maxVal[d]||candNN!=localNonNeg[d]){
                maxVal[d]=cand;localNonNeg[d]=candNN;upd=true;
            }
        }
        if(!upd)break;
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
    // Record all use PCs for each register so we can verify straight-line idx safety.
    std::vector<std::vector<std::size_t>> regUsePcs(fn.nextReg);
    for(std::size_t pc=0;pc<fn.code.size();++pc)
        for(Reg a:fn.code[pc].args)if(a<regUsePcs.size())regUsePcs[a].push_back(pc);
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
               definitions[*in.dest]==1&&in.args[1]<definitions.size()){
                bool idxSafe=(definitions[in.args[1]]==1);
                if(!idxSafe){
                    const Reg ptrReg=*in.dest;
                    const Reg idxReg=in.args[1];
                    idxSafe=!regUsePcs[ptrReg].empty();
                    for(std::size_t upc:regUsePcs[ptrReg]){
                        if(upc<=pc){idxSafe=false;break;}
                        for(std::size_t m=pc+1;m<=upc;++m){
                            const auto& mid=fn.code[m];
                            if(jumpTargets[m]||(m<upc&&(mid.op==NirOp::Jump||mid.op==NirOp::JumpIfFalse||mid.op==NirOp::Return||mid.op==NirOp::Throw))){
                                idxSafe=false;break;
                            }
                            if(m<upc&&mid.dest&&*mid.dest==idxReg){idxSafe=false;break;}
                        }
                        if(!idxSafe)break;
                    }
                }
                if(idxSafe){
                    directPtrs[*in.dest]=std::make_pair(root,in.args[1]);
                    continue;
                }
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
                else if(std::holds_alternative<std::monostate>(in.literal)){
                    out<<" "<<dst<<"=UINT64_C(0);\n";
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
            else if(in.text=="~")out<<" "<<dst<<"=(uint64_t)(~"<<lhs<<");\n";
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
                    (in.args[0]<localNonNeg.size()&&localNonNeg[in.args[0]]&&
                     ((in.args[1]<localNonNeg.size()&&localNonNeg[in.args[1]])||
                      (constantValues.count(in.args[1])&&constantValues.at(in.args[1])>0)));
                const bool u32Div = nonNegDiv &&
                    in.args[0]<maxVal.size()&&maxVal[in.args[0]]<=4294967295ULL&&
                    in.args[1]<maxVal.size()&&maxVal[in.args[1]]<=4294967295ULL;
                if(u32Div){
                    if(constantValues.count(in.args[1])&&constantValues.at(in.args[1])>0){
                        out<<" "<<dst<<"=(uint64_t)((uint32_t)"<<lhs<<" "<<in.text<<" UINT32_C("<<constantValues.at(in.args[1])<<"));\n";
                    }else{
                        out<<" { uint32_t _nq_d=(uint32_t)"<<rhs<<"; if(NQ_UNLIKELY(_nq_d==0))nq_trap(); "
                           <<dst<<"=(uint64_t)((uint32_t)"<<lhs<<" "<<in.text<<" _nq_d); }\n";
                    }
                }else{
                    out<<" "<<dst<<"=nq_div("<<lhs<<","<<rhs<<","<<(nonNegDiv?1:0)<<","<<(in.text=="%"?1:0)<<");\n";
                }
            }else if(in.text=="*"){
                const bool u32Mul =
                    in.args[0]<localNonNeg.size()&&localNonNeg[in.args[0]]&&
                    in.args[1]<localNonNeg.size()&&localNonNeg[in.args[1]]&&
                    in.args[0]<maxVal.size()&&maxVal[in.args[0]]<=4294967295ULL&&
                    in.args[1]<maxVal.size()&&maxVal[in.args[1]]<=4294967295ULL;
                if(u32Mul)
                    out<<" "<<dst<<"=((uint64_t)(uint32_t)"<<lhs<<"*(uint64_t)(uint32_t)"<<rhs<<");\n";
                else
                    out<<" "<<dst<<"=("<<lhs<<" * "<<rhs<<");\n";
            }else if(in.text=="<<"||in.text==">>"){
                out<<" "<<dst<<"=("<<((in.text==">>"&&!isUnsigned)?"(uint64_t)((int64_t)":"(uint64_t)(")<<lhs<<" "<<in.text<<" ((uint64_t)"<<rhs<<"&63ULL)));\n";
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
            }else if(target==TypeKind::Bool){
                out<<" "<<dst<<"=(uint64_t)("<<lhs<<"!=0);\n";
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
            }else if(in.text=="textLength"){
                out<<" "<<dst<<"=(uint64_t)(("<<lhs<<")?strlen((const char*)(uintptr_t)"<<lhs<<"):0);\n";
            }else if(in.text=="clockMillis"){
                out<<" "<<dst<<"=nq_clock_millis();\n";
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
            else if(in.args.size()==1)
                out<<" "<<dst<<"=(uint64_t)(uintptr_t)((char*)(uintptr_t)"<<lhs<<"+"<<in.target<<");\n";
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
        case NirOp::AtomicLoad:{
            const char* t=(in.width==1?"uint8_t":(in.width==2?"uint16_t":(in.width==4?"uint32_t":"uint64_t")));
            out<<" "<<dst<<"=(uint64_t)__atomic_load_n((const volatile "<<t<<"*)(uintptr_t)"<<lhs<<", __ATOMIC_SEQ_CST);\n";
            break;
        }
        case NirOp::AtomicStore:{
            const char* t=(in.width==1?"uint8_t":(in.width==2?"uint16_t":(in.width==4?"uint32_t":"uint64_t")));
            out<<" __atomic_store_n((volatile "<<t<<"*)(uintptr_t)"<<lhs<<", ("<<t<<")"<<rhs<<", __ATOMIC_SEQ_CST);\n";
            break;
        }
        case NirOp::AtomicExchange:{
            const char* t=(in.width==1?"uint8_t":(in.width==2?"uint16_t":(in.width==4?"uint32_t":"uint64_t")));
            out<<" "<<dst<<"=(uint64_t)__atomic_exchange_n((volatile "<<t<<"*)(uintptr_t)"<<lhs<<", ("<<t<<")"<<rhs<<", __ATOMIC_SEQ_CST);\n";
            break;
        }
        case NirOp::AtomicCompareExchange:{
            const char* t=(in.width==1?"uint8_t":(in.width==2?"uint16_t":(in.width==4?"uint32_t":"uint64_t")));
            out<<" { "<<t<<" _nq_exp=("<<t<<")"<<rhs<<"; __atomic_compare_exchange_n((volatile "<<t<<"*)(uintptr_t)"<<lhs
               <<", &_nq_exp, ("<<t<<")"<<args(in,2)<<", 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); "<<dst<<"=(uint64_t)_nq_exp; }\n";
            break;
        }
        case NirOp::AtomicFence:
            out<<" __atomic_thread_fence(__ATOMIC_SEQ_CST);\n";
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
