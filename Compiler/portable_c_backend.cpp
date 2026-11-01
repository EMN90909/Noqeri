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

namespace noe {
namespace {

bool unsignedInteger(TypeKind k) {
    return k==TypeKind::U8||k==TypeKind::U16||k==TypeKind::U32||
           k==TypeKind::U64||k==TypeKind::Usize;
}
bool supportedScalar(TypeKind k) {
    Type t;t.kind=k;
    return k==TypeKind::Bool||k==TypeKind::Float||k==TypeKind::String||
           (t.isInteger()&&t.size()>0&&t.size()<=8);
}
std::string reg(Reg r) { return "r[" + std::to_string(r) + "]"; }
std::string args(const NirInstruction& in,std::size_t index) {
    return index<in.args.size()?reg(in.args[index]):"UINT64_C(0)";
}
std::string quote(const std::string& s) {
    std::ostringstream out;out << '"';
    // Hex escapes can consume adjacent hex digits, so use fixed-width octal.
    for(unsigned char ch:s){
        if(ch=='"'||ch=='\\')out << '\\' << static_cast<char>(ch);
        else if(ch>=32&&ch<=126)out << static_cast<char>(ch);
        else {out << '\\' << char('0'+(ch>>6)) << char('0'+((ch>>3)&7)) << char('0'+(ch&7));}
    }
    out << '"';return out.str();
}
const char* runtime=R"C(
/* Portable C11 machine-code bridge. This file is generated from typed NIR.
   It requires a native C compiler, not a Noqeri NQRI bytecode interpreter. */
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static uint64_t nq_bits(double x){uint64_t b=0;memcpy(&b,&x,sizeof b);return b;}
static double nq_double(uint64_t b){double x=0;memcpy(&x,&b,sizeof x);return x;}
static void nq_trap(void){abort();}
static uint64_t nq_narrow(uint64_t n,unsigned width,int is_signed){
    if(width>=64)return n;
    uint64_t mask=(UINT64_C(1)<<width)-UINT64_C(1);
    n &= mask;
    if(is_signed&&(n&(UINT64_C(1)<<(width-1))))n|=~mask;
    return n;
}
static uint64_t nq_from_float(uint64_t bits,unsigned width,int is_unsigned){
    double d=nq_double(bits);
    if(!isfinite(d))nq_trap();
    double value=trunc(d);
    double upper=ldexp(1.0,is_unsigned?(int)width:(int)width-1);
    double lower=is_unsigned?0.0:-ldexp(1.0,(int)width-1);
    if(value<lower||value>=upper)nq_trap();
    if(is_unsigned)return nq_narrow((uint64_t)value,width,0);
    return nq_narrow((uint64_t)(int64_t)value,width,1);
}
static uint64_t nq_div(uint64_t a,uint64_t b,int is_unsigned,int remainder){
    if(b==0)nq_trap();
    if(is_unsigned)return remainder?a%b:a/b;
    int64_t x=(int64_t)a,y=(int64_t)b;
    if(x==INT64_MIN&&y==-1)nq_trap();
    return (uint64_t)(remainder?x%y:x/y);
}
static void nq_print(uint64_t bits,int kind){
    switch(kind){
      case 0:printf("%" PRId64 "\n",(int64_t)bits);break;
      case 1:printf("%" PRIu64 "\n",bits);break;
      case 2:puts(bits?"true":"false");break;
      case 3:printf("%.6f\n",nq_double(bits));break;
      case 4:if(!bits)nq_trap();puts((const char*)(uintptr_t)bits);break;
      default:nq_trap();
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
    // Validate *before* writing any partially usable portable output.
    for(const auto& in:fn.code){
        const bool valid=[&](){
            switch(in.op){
            case NirOp::Const:
                return !std::holds_alternative<std::monostate>(in.literal);
            case NirOp::Load:case NirOp::Store:case NirOp::Nop:
                return true;
            case NirOp::Unary:
                return in.args.size()==1&&(in.text=="!"||in.text=="-");
            case NirOp::Binary:
                return in.args.size()==2 &&
                    (in.text=="+"||in.text=="-"||in.text=="*"||
                     in.text=="/"||in.text=="%"||
                     in.text=="=="||in.text=="!="||in.text=="<"||
                     in.text=="<="||in.text==">"||in.text==">="||
                     in.text=="&&"||in.text=="||");
            case NirOp::Cast:{
                if(in.args.size()!=1)return false;
                Type src;
                if(in.args[0]<fn.registerTypes.size())src=fn.registerTypes[in.args[0]];
                return src.isNumeric()&&in.type.isNumeric();
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
        if(in.op==NirOp::Load||in.op==NirOp::Store)
            if(!vars.count(in.text))vars.emplace(in.text,vars.size());
    out<<"static uint64_t "<<symbol<<"(";
    if(fn.params.empty())out<<"void";
    for(std::size_t i=0;i<fn.params.size();++i){
        if(i)out<<", ";
        out<<"uint64_t p"<<i;
    }
    out<<"){\n uint64_t r["<<std::max<std::uint32_t>(fn.nextReg,1)<<"]={0};\n";
    out<<" uint64_t v["<<std::max<std::size_t>(vars.size(),1)<<"]={0};\n";
    for(std::size_t i=0;i<fn.params.size();++i)out<<" v["<<vars.at(fn.params[i])<<"]=p"<<i<<";\n";
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];const auto dst=in.dest?reg(*in.dest):"r[0]";
        const TypeKind srcKind=!in.args.empty()&&in.args[0]<fn.registerTypes.size()?
            fn.registerTypes[in.args[0]].kind:TypeKind::Unknown;
        const auto lhs=args(in,0),rhs=args(in,1);
        out<<"L"<<pc<<":\n";
        switch(in.op){
        case NirOp::Const:
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
            break;
        case NirOp::Store:out<<" v["<<vars.at(in.text)<<"]="<<lhs<<";\n";break;
        case NirOp::Load:out<<" "<<dst<<"=v["<<vars.at(in.text)<<"];\n";break;
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
                out<<" "<<dst<<"=nq_div("<<lhs<<","<<rhs<<","<<(isUnsigned?1:0)<<","<<(in.text=="%"?1:0)<<");\n";
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
                    else if(unsignedInteger(srcKind))type=0; // Stage-0 print currently renders u64 bit patterns as signed.
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
        default:break;
        }
    }
    out<<"L"<<fn.code.size()<<":\n return UINT64_C(0);\n}\n";
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
    for(const auto& fn:program.functions)
        if(!emitFunction(out,fn,symbols,diagnostics,symbols.at(fn.name)))return false;
    if(!emitFunction(out,program.entry,symbols,diagnostics,"nq_entry"))return false;
    out<<"int main(void){(void)nq_entry();return 0;}\n";
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
