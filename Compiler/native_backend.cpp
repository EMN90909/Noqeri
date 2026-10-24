#include "noe.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {

enum class ValueKind{Unknown,Int,Bool,String,Float,Null};
bool isUnsigned(TypeKind kind){
    return kind==TypeKind::U8||kind==TypeKind::U16||kind==TypeKind::U32||
           kind==TypeKind::U64||kind==TypeKind::Usize;
}
// Emit a precise u64 quotient/remainder for a known divisor, using one
// 64x64 -> 128-bit multiply plus at most one correction. No LLVM required.
// The reciprocal floor(2^64/d) underestimates floor(n/d) by at most one.
void emitUnsignedConstantDivision(std::ostream& out,std::uint64_t divisor,
                                 bool remainder,const std::string& done){
    if(divisor==1){
        if(remainder)out<<"    xor eax, eax\n";
        return;
    }
    if((divisor&(divisor-1))==0){
        unsigned shift=0;
        for(auto value=divisor;value>1;value>>=1)++shift;
        if(remainder){
            out<<"    mov r10, "<<(divisor-1)<<"\n    and rax, r10\n";
        }else out<<"    shr rax, "<<shift<<"\n";
        return;
    }
    const auto max=std::numeric_limits<std::uint64_t>::max();
    const auto reciprocal=max/divisor+((max%divisor)==divisor-1?1u:0u);
    out<<"    mov r11, rax\n"
       <<"    mov r10, "<<reciprocal<<"\n"
       <<"    mul r10\n"
       <<"    mov rax, rdx\n"
       <<"    mov r10, "<<divisor<<"\n"
       <<"    imul rdx, r10\n"
       <<"    sub r11, rdx\n"
       <<"    cmp r11, r10\n"
       <<"    jb "<<done<<"\n"
       <<"    inc rax\n"
       <<"    sub r11, r10\n"
       <<done<<":\n";
    if(remainder)out<<"    mov rax, r11\n";
}

ValueKind literalKind(const NirValue&v){if(std::holds_alternative<std::int64_t>(v))return ValueKind::Int;if(std::holds_alternative<bool>(v))return ValueKind::Bool;if(std::holds_alternative<std::string>(v))return ValueKind::String;if(std::holds_alternative<double>(v))return ValueKind::Float;return ValueKind::Null;}
bool isComparison(const std::string&op){return op=="=="||op=="!="||op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||";}
std::string safe(const std::string&name){std::string out;for(char c:name)out+=(std::isalnum(static_cast<unsigned char>(c))||c=='_')?c:'_';return out;}
std::string internalSymbol(const std::string&name){return"noqeri_fn_"+safe(name);}
std::string pcLabel(const NirFunction&fn,std::size_t pc){return".L_"+safe(fn.name)+"_"+std::to_string(pc);}
std::string localLabel(const NirFunction&fn,std::size_t pc,const std::string&suffix){return".L_"+safe(fn.name)+"_"+std::to_string(pc)+"_"+suffix;}
std::size_t align16(std::size_t n){return(n+15u)&~std::size_t(15u);}

struct FunctionLayout{std::vector<std::string>variables;std::unordered_map<std::string,std::size_t>variableIndex;std::vector<ValueKind>regKinds;std::unordered_map<std::string,ValueKind>varKinds;};
FunctionLayout layoutFor(const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns){
    FunctionLayout l;l.regKinds.assign(fn.nextReg,ValueKind::Unknown);auto add=[&](const std::string&n){if(!l.variableIndex.count(n)){l.variableIndex[n]=l.variables.size();l.variables.push_back(n);}};
    for(const auto&p:fn.params)add(p);for(const auto&i:fn.code)if(i.op==NirOp::Load||i.op==NirOp::Store||i.op==NirOp::AddressOf)add(i.text);
    for(const auto&i:fn.code){auto rk=[&](Reg r){return r<l.regKinds.size()?l.regKinds[r]:ValueKind::Unknown;};
        if(i.op==NirOp::Const&&i.dest)l.regKinds[*i.dest]=literalKind(i.literal);
        else if(i.op==NirOp::Store&&!i.args.empty())l.varKinds[i.text]=rk(i.args[0]);
        else if(i.op==NirOp::Load&&i.dest){auto it=l.varKinds.find(i.text);l.regKinds[*i.dest]=it==l.varKinds.end()?ValueKind::Unknown:it->second;}
        else if(i.op==NirOp::Unary&&i.dest&&!i.args.empty())l.regKinds[*i.dest]=i.text=="!"?ValueKind::Bool:rk(i.args[0]);
        else if((i.op==NirOp::AddressOf||i.op==NirOp::LoadMemory||i.op==NirOp::PtrOffset||i.op==NirOp::Cast||i.op==NirOp::StackAlloc||i.op==NirOp::MakeSlice||i.op==NirOp::SliceData||i.op==NirOp::SliceLen||i.op==NirOp::AtomicLoad||i.op==NirOp::AtomicExchange||i.op==NirOp::AtomicCompareExchange||i.op==NirOp::Try||i.op==NirOp::Intrinsic)&&i.dest)l.regKinds[*i.dest]=ValueKind::Int;
        else if(i.op==NirOp::InlineAsm&&i.dest)l.regKinds[*i.dest]=ValueKind::Null;
        else if(i.op==NirOp::Binary&&i.dest&&i.args.size()==2){if(isComparison(i.text))l.regKinds[*i.dest]=ValueKind::Bool;else{auto a=rk(i.args[0]),b=rk(i.args[1]);if(a==ValueKind::String||b==ValueKind::String)l.regKinds[*i.dest]=ValueKind::String;else if(a==ValueKind::Float||b==ValueKind::Float)l.regKinds[*i.dest]=ValueKind::Float;else l.regKinds[*i.dest]=ValueKind::Int;}}
        else if(i.op==NirOp::Call&&i.dest){if(i.text=="print")l.regKinds[*i.dest]=ValueKind::Null;else if(i.text=="clockMillis"||i.text=="textLength")l.regKinds[*i.dest]=ValueKind::Int;else if(i.text=="platform")l.regKinds[*i.dest]=ValueKind::String;else{auto it=returns.find(i.text);l.regKinds[*i.dest]=it==returns.end()?ValueKind::Unknown:it->second;}}
    }
    return l;
}
ValueKind inferReturn(const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns){auto l=layoutFor(fn,returns);ValueKind result=ValueKind::Null;for(const auto&i:fn.code)if(i.op==NirOp::Return&&!i.args.empty()&&i.args[0]<l.regKinds.size()){auto k=l.regKinds[i.args[0]];if(result==ValueKind::Null||result==ValueKind::Unknown)result=k;else if(k!=ValueKind::Unknown&&result!=k)result=ValueKind::Unknown;}return result;}
std::string regSlot(Reg r){return"QWORD PTR [rbp-"+std::to_string((static_cast<std::size_t>(r)+1)*8)+"]";}
std::size_t varOffset(const NirFunction&fn,const FunctionLayout&l,const std::string&name){auto it=l.variableIndex.find(name);auto index=static_cast<std::size_t>(fn.nextReg)+(it==l.variableIndex.end()?0:it->second)+1;return index*8;}
std::string varSlot(const NirFunction&fn,const FunctionLayout&l,const std::string&name){return"QWORD PTR [rbp-"+std::to_string(varOffset(fn,l,name))+"]";}
std::string varAddress(const NirFunction&fn,const FunctionLayout&l,const std::string&name){return"[rbp-"+std::to_string(varOffset(fn,l,name))+"]";}
std::string symbolForCall(const NirProgram&p,const std::string&name){for(const auto&f:p.functions)if(f.name==name)return(f.isExtern||f.isExport)?safe(f.name):internalSymbol(f.name);return safe(name);}

void emitRuntime(std::ostream&out){out<<R"ASM(
.data
.align 8
noqeri_abi_current:
    .quad 0
.text
noqeri_rt_write:
    mov r11, QWORD PTR [rip + noqeri_abi_current]
    test r11, r11
    jz .L_noqeri_write_fail
    mov r10, QWORD PTR [r11 + 16]
    test r10, r10
    jz .L_noqeri_write_fail
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, QWORD PTR [r11 + 8]
    sub rsp, 8
    call r10
    add rsp, 8
    ret
.L_noqeri_write_fail:
    mov eax, -1
    ret
noqeri_rt_print_cstr:
    push rbp
    mov rbp, rsp
    push rdi
    xor rsi, rsi
.L_noqeri_strlen:
    cmp BYTE PTR [rdi + rsi], 0
    je .L_noqeri_string_ready
    inc rsi
    jmp .L_noqeri_strlen
.L_noqeri_string_ready:
    pop rdi
    call noqeri_rt_write
    lea rdi, [rip + .L_noqeri_newline]
    mov rsi, 1
    call noqeri_rt_write
    pop rbp
    ret
noqeri_rt_print_bool:
    test rdi, rdi
    jz .L_noqeri_false
    lea rdi, [rip + .L_noqeri_true]
    jmp noqeri_rt_print_cstr
.L_noqeri_false:
    lea rdi, [rip + .L_noqeri_false_text]
    jmp noqeri_rt_print_cstr
noqeri_rt_text_length:
    xor eax, eax
    test rdi, rdi
    jz .L_noqeri_text_length_done
.L_noqeri_text_length_loop:
    cmp BYTE PTR [rdi + rax], 0
    je .L_noqeri_text_length_done
    inc rax
    jmp .L_noqeri_text_length_loop
.L_noqeri_text_length_done:
    ret
noqeri_rt_print_int:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov rax, rdi
    lea rsi, [rbp - 2]
    mov BYTE PTR [rsi], 0
    xor rcx, rcx
    xor r8d, r8d
    test rax, rax
    jge .L_noqeri_int_abs
    mov r8b, 1
    neg rax
.L_noqeri_int_abs:
    test rax, rax
    jne .L_noqeri_int_digits
    dec rsi
    mov BYTE PTR [rsi], 48
    inc rcx
    jmp .L_noqeri_int_sign
.L_noqeri_int_digits:
    xor rdx, rdx
    mov r9, 10
    div r9
    add dl, 48
    dec rsi
    mov BYTE PTR [rsi], dl
    inc rcx
    test rax, rax
    jne .L_noqeri_int_digits
.L_noqeri_int_sign:
    test r8b, r8b
    jz .L_noqeri_int_write
    dec rsi
    mov BYTE PTR [rsi], 45
    inc rcx
.L_noqeri_int_write:
    mov rdi, rsi
    mov rsi, rcx
    call noqeri_rt_write
    lea rdi, [rip + .L_noqeri_newline]
    mov rsi, 1
    call noqeri_rt_write
    leave
    ret
noqeri_rt_clock_millis:
    mov r11, QWORD PTR [rip + noqeri_abi_current]
    test r11, r11
    jz .L_noqeri_clock_fail
    mov r10, QWORD PTR [r11 + 24]
    test r10, r10
    jz .L_noqeri_clock_fail
    mov rdi, QWORD PTR [r11 + 8]
    sub rsp, 8
    call r10
    add rsp, 8
    ret
.L_noqeri_clock_fail:
    xor eax, eax
    ret
noqeri_rt_platform:
    mov r11, QWORD PTR [rip + noqeri_abi_current]
    test r11, r11
    jz .L_noqeri_platform_fail
    mov r10, QWORD PTR [r11 + 32]
    test r10, r10
    jz .L_noqeri_platform_fail
    mov rdi, QWORD PTR [r11 + 8]
    sub rsp, 8
    call r10
    add rsp, 8
    ret
.L_noqeri_platform_fail:
    lea rax, [rip + .L_noqeri_standalone]
    ret
)ASM";}
bool signedNarrow(TypeKind kind){
    return kind==TypeKind::I8||kind==TypeKind::I16||kind==TypeKind::I32;
}
void emitLoadWidth(std::ostream&out,std::size_t width,bool signExtend=false){
    if(width==1)out<<(signExtend?"    movsx rax, BYTE PTR [rax]\n":
                                      "    movzx eax, BYTE PTR [rax]\n");
    else if(width==2)out<<(signExtend?"    movsx rax, WORD PTR [rax]\n":
                                           "    movzx eax, WORD PTR [rax]\n");
    else if(width==4)out<<(signExtend?"    movsxd rax, DWORD PTR [rax]\n":
                                           "    mov eax, DWORD PTR [rax]\n");
    else out<<"    mov rax, QWORD PTR [rax]\n";
}
void emitStoreWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    mov BYTE PTR [rax], r10b\n";else if(width==2)out<<"    mov WORD PTR [rax], r10w\n";else if(width==4)out<<"    mov DWORD PTR [rax], r10d\n";else out<<"    mov QWORD PTR [rax], r10\n";}
// A single-use PtrOffset feeding an adjacent memory instruction does not
// require an intermediate LEA or virtual pointer register. Keep the operand
// width/sign rules identical to ordinary LoadMemory and StoreMemory.
void emitIndexedLoad(std::ostream&out,std::size_t width,bool signedValue,std::size_t scale){
    const std::string address="[rax + r11*"+std::to_string(scale)+"]";
    if(width==1)out<<(signedValue?"    movsx rax, BYTE PTR ":"    movzx eax, BYTE PTR ")<<address<<"\n";
    else if(width==2)out<<(signedValue?"    movsx rax, WORD PTR ":"    movzx eax, WORD PTR ")<<address<<"\n";
    else if(width==4)out<<(signedValue?"    movsxd rax, DWORD PTR ":"    mov eax, DWORD PTR ")<<address<<"\n";
    else out<<"    mov rax, QWORD PTR "<<address<<"\n";
}
void emitIndexedStore(std::ostream&out,std::size_t width,std::size_t scale){
    const std::string address="[rax + r11*"+std::to_string(scale)+"]";
    if(width==1)out<<"    mov BYTE PTR "<<address<<", r10b\n";
    else if(width==2)out<<"    mov WORD PTR "<<address<<", r10w\n";
    else if(width==4)out<<"    mov DWORD PTR "<<address<<", r10d\n";
    else out<<"    mov QWORD PTR "<<address<<", r10\n";
}
void emitExchangeWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    xchg BYTE PTR [rax], r10b\n    movzx eax, r10b\n";else if(width==2)out<<"    xchg WORD PTR [rax], r10w\n    movzx eax, r10w\n";else if(width==4)out<<"    xchg DWORD PTR [rax], r10d\n    mov eax, r10d\n";else out<<"    xchg QWORD PTR [rax], r10\n    mov rax, r10\n";}
void emitAtomicStoreWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    xchg BYTE PTR [rax], r10b\n";else if(width==2)out<<"    xchg WORD PTR [rax], r10w\n";else if(width==4)out<<"    xchg DWORD PTR [rax], r10d\n";else out<<"    xchg QWORD PTR [rax], r10\n";}
void emitCompareExchangeWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    lock cmpxchg BYTE PTR [r10], cl\n    movzx eax, al\n";else if(width==2)out<<"    lock cmpxchg WORD PTR [r10], cx\n    movzx eax, ax\n";else if(width==4)out<<"    lock cmpxchg DWORD PTR [r10], ecx\n    mov eax, eax\n";else out<<"    lock cmpxchg QWORD PTR [r10], rcx\n";}
void emitCast(std::ostream&out,const std::string&t){if(t=="u8")out<<"    and rax, 255\n";else if(t=="u16")out<<"    and rax, 65535\n";else if(t=="u32")out<<"    mov eax, eax\n";else if(t=="i8")out<<"    movsx rax, al\n";else if(t=="i16")out<<"    movsx rax, ax\n";else if(t=="i32")out<<"    movsxd rax, eax\n";}
void normalizeIntegerResult(std::ostream&out,TypeKind kind){
    switch(kind){
        case TypeKind::U8:emitCast(out,"u8");break;
        case TypeKind::U16:emitCast(out,"u16");break;
        case TypeKind::U32:emitCast(out,"u32");break;
        case TypeKind::I8:emitCast(out,"i8");break;
        case TypeKind::I16:emitCast(out,"i16");break;
        case TypeKind::I32:emitCast(out,"i32");break;
        default:break;
    }
}
bool safeInlineAsm(const std::string& text){if(text.empty())return false;if(text.find('\n')!=std::string::npos||text.find('\r')!=std::string::npos||text.find(':')!=std::string::npos)return false;auto p=text.find_first_not_of(" \t");return p!=std::string::npos&&text[p]!='.';}

bool emitFunction(std::ostream&out,const NirProgram&p,const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns,const std::map<std::string,std::string>&strings,Diagnostics&diagnostics){
    if(fn.isExtern)return true;
    auto l=layoutFor(fn,returns);const char*args[]={"rdi","rsi","rdx","rcx","r8","r9"};
    // Physically allocate NIR registers while reserving volatile rax/r10/r11
    // for instruction lowering. Disable allocation around unmodelled inline-asm
    // clobbers, until inline-asm constraints are implemented.
    // Constant divisors are discovered at emission time. Casted positive
    // integer literals remain constants even when optimization has not
    // folded the Cast instruction itself.
    std::unordered_map<Reg,std::int64_t> constantValues;
    std::unordered_map<Reg,unsigned> definitions;
    for(const auto& in:fn.code)if(in.dest)++definitions[*in.dest];
    for(const auto& in:fn.code){
        if(!in.dest||definitions[*in.dest]!=1)continue;
        if(in.op==NirOp::Const){
            if(auto v=std::get_if<std::int64_t>(&in.literal))constantValues[*in.dest]=*v;
        }else if(in.op==NirOp::Cast&&in.args.size()==1&&
                 (in.text=="usize"||in.text=="u64"||in.text=="int"||
                  in.text=="i64"||in.text=="isize")){
            const auto it=constantValues.find(in.args[0]);
            if(it!=constantValues.end())constantValues[*in.dest]=it->second;
        }else if(in.op==NirOp::Unary&&in.text=="-"&&in.args.size()==1){
            const auto it=constantValues.find(in.args[0]);
            if(it!=constantValues.end()){
                const std::uint64_t bits=std::uint64_t{0}-
                    static_cast<std::uint64_t>(it->second);
                std::int64_t value=0;
                std::memcpy(&value,&bits,sizeof(value));
                constantValues[*in.dest]=value;
            }
        }
    }
    std::unordered_map<Reg,std::string> physical;
    std::vector<std::string> saved;
    const bool hasInlineAsm=std::any_of(fn.code.begin(),fn.code.end(),
        [](const NirInstruction& i){return i.op==NirOp::InlineAsm;});
    // Reserve RBX for the most frequently referenced named local in loops.
    // Address-taken locals and inline-asm functions stay on the stack.
    // RBX is callee-saved and never used by virtual-register allocation.
    std::unordered_map<std::string,std::string> variablePhysical;
    if(!hasInlineAsm&&!fn.code.empty()){
        std::unordered_set<std::string> addressTaken,initialized;
        for(const auto& name:fn.params)initialized.insert(name);
        for(const auto& in:fn.code){
            // A volatile named local must remain backed by memory,
            // even if its address is never explicitly taken.
            if(in.op==NirOp::AddressOf||
               (in.isVolatile&&(in.op==NirOp::Load||
                                in.op==NirOp::Store)))
                addressTaken.insert(in.text);
            if(in.op==NirOp::Store)initialized.insert(in.text);
        }
        std::vector<int> depthDiff(fn.code.size()+1,0);
        for(std::size_t pc=0;pc<fn.code.size();++pc){
            const auto& in=fn.code[pc];
            if(in.op==NirOp::Jump&&in.target<pc){
                ++depthDiff[in.target];
                --depthDiff[pc+1];
            }
        }
        std::map<std::string,std::size_t> hot;
        int depth=0;
        for(std::size_t pc=0;pc<fn.code.size();++pc){
            depth+=depthDiff[pc];
            const auto& in=fn.code[pc];
            if(depth>0&&(in.op==NirOp::Load||in.op==NirOp::Store)&&
               !addressTaken.count(in.text)&&initialized.count(in.text))
                hot[in.text]+=static_cast<std::size_t>(std::min(depth,4))*
                    (in.op==NirOp::Load?2u:1u);
        }
        // The first hot local takes RBX; two additional truly hot locals
        // can use R12/R13, leaving R14/R15 for virtual temporaries.
        // Deterministic ranking keeps emitted code reproducible.
        std::vector<std::pair<std::string,std::size_t>> ranked(
            hot.begin(),hot.end());
        std::sort(ranked.begin(),ranked.end(),
            [](const auto& a,const auto& b){
                return a.second>b.second||
                       (a.second==b.second&&a.first<b.first);
            });
        const std::vector<std::string> pool{"rbx","r12","r13"};
        for(std::size_t n=0;n<pool.size()&&n<ranked.size();++n){
            if(ranked[n].second<(n==0?2u:6u))break;
            variablePhysical.emplace(ranked[n].first,pool[n]);
            saved.push_back(pool[n]);
        }
    }
    if(!hasInlineAsm){
        std::vector<std::string> available;
        for(const auto* reg:{"r12","r13","r14","r15"}){
            if(std::find(saved.begin(),saved.end(),reg)==saved.end())
                available.emplace_back(reg);
        }
        const auto plan=LinearScanRegisterAllocator{}.allocate(fn,available);
        for(const auto& loc:plan.locations)
            if(!loc.spilled&&!loc.physical.empty())physical.emplace(loc.value,loc.physical);
        for(const auto* reg:{"r12","r13","r14","r15"})
            if(std::any_of(physical.begin(),physical.end(),
                [&](const auto& entry){return entry.second==reg;}))saved.emplace_back(reg);
    }
    if(fn.params.size()>6){diagnostics.error("NQR-N5004",{},"Noqeri x86-64 calling convention currently supports at most 6 parameters");return false;}
    std::string symbol=fn.isExport?safe(fn.name):internalSymbol(fn.name);
    if(fn.isExport)out<<"\n.global "<<symbol<<"\n";
    std::vector<std::size_t> fixedStorage(fn.code.size(),0);
    std::size_t frameBytes=(static_cast<std::size_t>(fn.nextReg)+l.variables.size())*8;
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto&i=fn.code[pc];
        if(i.op==NirOp::StackAlloc){
            frameBytes+=align16(std::max<std::size_t>(i.width,1));
            fixedStorage[pc]=frameBytes;
        }else if(i.op==NirOp::MakeSlice){
            frameBytes+=16;
            fixedStorage[pc]=frameBytes;
        }
    }
    // Saved register slots follow all fixed objects without shifting the
    // rbp-relative locations of address-taken variables or slices.
    const std::size_t saveBase=align16(frameBytes);
    const std::size_t frame=align16(saveBase+8*saved.size());
    const auto savedSlot=[&](std::size_t n){
        return "QWORD PTR [rbp-"+std::to_string(saveBase+8*(n+1))+"]";
    };
    out<<"\n"<<symbol<<":\n    push rbp\n    mov rbp, rsp\n";if(frame)out<<"    sub rsp, "<<frame<<"\n";
    for(std::size_t n=0;n<saved.size();++n)out<<"    mov "<<savedSlot(n)<<", "<<saved[n]<<"\n";
    const auto restoreSaved=[&](){
        for(std::size_t n=saved.size();n-->0;)
            out<<"    mov "<<saved[n]<<", "<<savedSlot(n)<<"\n";
    };
    const auto variableOperand=[&](const std::string& name){
        const auto it=variablePhysical.find(name);
        return it!=variablePhysical.end()?it->second:varSlot(fn,l,name);
    };
    for(std::size_t i=0;i<fn.params.size();++i)
        out<<"    mov "<<variableOperand(fn.params[i])<<", "<<args[i]<<"\n";
    const auto load=[&](Reg r,const char*cpu){
        auto it=physical.find(r);
        const std::string source=it==physical.end()?regSlot(r):it->second;
        if(source!=cpu)out<<"    mov "<<cpu<<", "<<source<<"\n";
    };
    const auto store=[&](Reg r,const char*cpu){
        auto it=physical.find(r);
        const std::string target=it==physical.end()?regSlot(r):it->second;
        if(target!=cpu)out<<"    mov "<<target<<", "<<cpu<<"\n";
    };
    // Emitting one unused assembler label per NIR opcode forces the assembler
    // and linker to process needless symbols. Only branch destinations need
    // PC labels; diagnostic/trap labels remain emitted at their own sites.
    std::unordered_set<std::size_t> jumpTargets;
    std::unordered_map<Reg,std::size_t> useCounts;
    for(const auto& i:fn.code){
        if(i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse)
            jumpTargets.insert(i.target);
        for(auto r:i.args)++useCounts[r];
    }
    std::unordered_set<std::size_t> fusedBranches;
    std::unordered_set<std::size_t> fusedPointers;
    for(std::size_t pc=0;pc+1<fn.code.size();++pc){
        const auto& pointer=fn.code[pc];
        const auto& access=fn.code[pc+1];
        if(pointer.op!=NirOp::PtrOffset||!pointer.dest||
           pointer.args.size()!=2||
           (pointer.width!=1&&pointer.width!=2&&
            pointer.width!=4&&pointer.width!=8)||
           jumpTargets.count(pc+1)||useCounts[*pointer.dest]!=1)
            continue;
        if((access.op==NirOp::LoadMemory||access.op==NirOp::StoreMemory)&&
           !access.args.empty()&&access.args[0]==*pointer.dest)
            fusedPointers.insert(pc);
    }
    for(std::size_t pc=0;pc<fn.code.size();++pc){const auto&i=fn.code[pc];
        if(fusedBranches.count(pc))continue;
        if(jumpTargets.count(pc))out<<pcLabel(fn,pc)<<":\n";
        switch(i.op){
        case NirOp::Const:{if(!i.dest)break;if(auto v=std::get_if<std::int64_t>(&i.literal))out<<"    mov rax, "<<*v<<"\n";else if(auto v=std::get_if<bool>(&i.literal))out<<"    mov rax, "<<(*v?1:0)<<"\n";else if(auto v=std::get_if<std::string>(&i.literal)){auto s=strings.find(*v);if(s==strings.end()){diagnostics.error("NQR-N5005",{},"internal string-pool error");return false;}out<<"    lea rax, [rip + "<<s->second<<"]\n";}else if(std::holds_alternative<std::monostate>(i.literal))out<<"    xor eax, eax\n";else{diagnostics.error("NQR-N5001",{},"float native code generation is not implemented yet");return false;}store(*i.dest,"rax");break;}
        case NirOp::Load:if(i.dest){out<<"    mov rax, "<<variableOperand(i.text)<<"\n";store(*i.dest,"rax");}break;
        case NirOp::Store:if(!i.args.empty()){load(i.args[0],"rax");out<<"    mov "<<variableOperand(i.text)<<", rax\n";}break;
        case NirOp::AddressOf:if(i.dest){out<<"    lea rax, "<<varAddress(fn,l,i.text)<<"\n";store(*i.dest,"rax");}break;
        case NirOp::LoadMemory:if(i.dest&&!i.args.empty()){
            const bool sign=*i.dest<fn.registerTypes.size()&&
                signedNarrow(fn.registerTypes[*i.dest].kind);
            if(pc>0&&fusedPointers.count(pc-1)){
                const auto& pointer=fn.code[pc-1];
                load(pointer.args[0],"rax");
                load(pointer.args[1],"r11");
                emitIndexedLoad(out,i.width,sign,pointer.width);
            }else{
                load(i.args[0],"rax");
                emitLoadWidth(out,i.width,sign);
            }
            store(*i.dest,"rax");
        }break;
        case NirOp::StoreMemory:if(i.args.size()==2){
            if(pc>0&&fusedPointers.count(pc-1)){
                const auto& pointer=fn.code[pc-1];
                load(pointer.args[0],"rax");
                load(pointer.args[1],"r11");
                load(i.args[1],"r10");
                emitIndexedStore(out,i.width,pointer.width);
            }else{
                load(i.args[0],"rax");
                load(i.args[1],"r10");
                emitStoreWidth(out,i.width);
            }
        }break;
        case NirOp::PtrOffset:if(fusedPointers.count(pc))break;else if(i.dest&&!i.args.empty()){load(i.args[0],"rax");if(i.args.size()>1){load(i.args[1],"r10");if(i.width==1||i.width==2||i.width==4||i.width==8)
                        out<<"    lea rax, [rax + r10*"<<i.width<<"]\n";
                    else{
                        out<<"    imul r10, "<<i.width<<"\n";
                        out<<"    add rax, r10\n";
                    }}else if(i.target)out<<"    add rax, "<<i.target<<"\n";store(*i.dest,"rax");}break;
        case NirOp::StackAlloc:if(i.dest){if(pc>=fixedStorage.size()||fixedStorage[pc]==0){diagnostics.error("NQR-N5024",{},"internal native stack-allocation layout error");return false;}out<<"    lea rax, [rbp-"<<fixedStorage[pc]<<"]\n";store(*i.dest,"rax");}break;
        case NirOp::MakeSlice:if(i.dest&&i.args.size()==2){if(pc>=fixedStorage.size()||fixedStorage[pc]<16){diagnostics.error("NQR-N5025",{},"internal native slice layout error");return false;}out<<"    lea r11, [rbp-"<<fixedStorage[pc]<<"]\n";load(i.args[0],"rax");out<<"    mov QWORD PTR [r11], rax\n";load(i.args[1],"rax");out<<"    mov QWORD PTR [r11+8], rax\n    mov rax, r11\n";store(*i.dest,"rax");}break;
        case NirOp::SliceData:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");out<<"    mov rax, QWORD PTR [rax]\n";store(*i.dest,"rax");}break;
        case NirOp::SliceLen:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");out<<"    mov rax, QWORD PTR [rax+8]\n";store(*i.dest,"rax");}break;
        case NirOp::CheckNonNull:{
            if(i.args.size()!=1){diagnostics.error("NQR-N5022",{},"malformed NIR null-check instruction");return false;}
            load(i.args[0],"rax");
            const auto valid=localLabel(fn,pc,"nonnull_ok");
            out<<"    test rax, rax\n    jnz "<<valid<<"\n    ud2\n"<<valid<<":\n";
            break;
        }
        case NirOp::CheckBounds:{
            if(i.args.size()!=2){diagnostics.error("NQR-N5023",{},"malformed NIR bounds-check instruction");return false;}
            load(i.args[0],"rax");
            const auto failed=localLabel(fn,pc,"bounds_fail");
            const auto valid=localLabel(fn,pc,"bounds_ok");
            const auto fixed=constantValues.find(i.args[1]);
            // A fixed non-negative length <= INT64_MAX needs only one
            // unsigned comparison: every negative i64 index is encoded above
            // every representable fixed length. Dynamic slice bounds retain
            // the explicit negative-index test.
            if(fixed!=constantValues.end()&&fixed->second>=0){
                if(fixed->second<=std::numeric_limits<std::int32_t>::max())
                    out<<"    cmp rax, "<<fixed->second<<"\n";
                else{
                    out<<"    mov r10, "<<fixed->second<<"\n";
                    out<<"    cmp rax, r10\n";
                }
                out<<"    jb "<<valid<<"\n    ud2\n"<<valid<<":\n";
            }else{
                out<<"    test rax, rax\n    js "<<failed<<"\n";
                load(i.args[1],"r10");
                out<<"    cmp rax, r10\n    jb "<<valid<<"\n"<<failed<<":\n    ud2\n"<<valid<<":\n";
            }
            break;
        }
        case NirOp::AtomicLoad:if(i.dest&&!i.args.empty()){out<<"    mfence\n";load(i.args[0],"rax");emitLoadWidth(out,i.width,*i.dest<fn.registerTypes.size()&&
            signedNarrow(fn.registerTypes[*i.dest].kind));out<<"    mfence\n";store(*i.dest,"rax");}break;
        case NirOp::AtomicStore:if(i.args.size()==2){load(i.args[0],"rax");load(i.args[1],"r10");emitAtomicStoreWidth(out,i.width);}break;
        case NirOp::AtomicExchange:if(i.dest&&i.args.size()==2){load(i.args[0],"rax");load(i.args[1],"r10");emitExchangeWidth(out,i.width);store(*i.dest,"rax");}break;
        case NirOp::AtomicCompareExchange:if(i.dest&&i.args.size()==3){load(i.args[0],"r10");load(i.args[1],"rax");load(i.args[2],"rcx");emitCompareExchangeWidth(out,i.width);store(*i.dest,"rax");}break;
        case NirOp::AtomicFence:out<<"    mfence\n";break;
        case NirOp::Intrinsic:{if(i.text=="x86.pause")out<<"    pause\n    xor eax, eax\n";else if(i.text=="x86.rdtsc")out<<"    rdtsc\n    shl rdx, 32\n    or rax, rdx\n";else if(i.text=="x86.halt")out<<"    hlt\n    xor eax, eax\n";else if(i.text=="compiler.fence")out<<"    mfence\n    xor eax, eax\n";else{diagnostics.error("NQR-N5020",{},"unsupported native intrinsic '"+i.text+"'");return false;}if(i.dest)store(*i.dest,"rax");break;}
        case NirOp::InlineAsm:if(!safeInlineAsm(i.text)){diagnostics.error("NQR-N5021",{},"inline asm must be one instruction and may not contain labels or assembler directives");return false;}out<<"    "<<i.text<<"\n";if(i.dest){out<<"    xor eax, eax\n";store(*i.dest,"rax");}break;
        case NirOp::Try:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");auto ok=localLabel(fn,pc,"try_ok");out<<"    test rax, rax\n    jns "<<ok<<"\n";restoreSaved();out<<"    leave\n    ret\n"<<ok<<":\n";store(*i.dest,"rax");}break;
        case NirOp::Throw:if(!i.args.empty()){load(i.args[0],"rax");auto encoded=localLabel(fn,pc,"throw_encoded");out<<"    test rax, rax\n    js "<<encoded<<"\n    neg rax\n    sub rax, 1\n"<<encoded<<":\n";restoreSaved();out<<"    leave\n    ret\n";}break;
        case NirOp::Cast:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");emitCast(out,i.text);store(*i.dest,"rax");}break;
        case NirOp::Unary:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");if(i.text=="-"){out<<"    neg rax\n";normalizeIntegerResult(out,i.type.kind);}else if(i.text=="!")out<<"    test rax, rax\n    sete al\n    movzx rax, al\n";store(*i.dest,"rax");}break;
        case NirOp::Binary:{if(!i.dest||i.args.size()!=2)break;auto a=i.args[0]<l.regKinds.size()?l.regKinds[i.args[0]]:ValueKind::Unknown,b=i.args[1]<l.regKinds.size()?l.regKinds[i.args[1]]:ValueKind::Unknown;if(a==ValueKind::Float||b==ValueKind::Float){diagnostics.error("NQR-N5001",{},"float native arithmetic is not implemented yet");return false;}if(a==ValueKind::String||b==ValueKind::String){diagnostics.error("NQR-N5002",{},"runtime string binary operations are not implemented yet");return false;}const auto operandKind=i.args[0]<fn.registerTypes.size()?
            fn.registerTypes[i.args[0]].kind:TypeKind::Unknown;
        const bool unsign=isUnsigned(operandKind)||
            (i.args[1]<fn.registerTypes.size()&&
             isUnsigned(fn.registerTypes[i.args[1]].kind));
        // If a comparison feeds only the immediately following conditional
        // branch, emit one compare and a direct inverted jcc. Preserve NIR
        // short-circuit structure: RHS evaluation is never speculated.
        const bool singleUse=i.dest&&useCounts[*i.dest]==1;
        const bool adjacent=pc+1<fn.code.size()&&
            fn.code[pc+1].op==NirOp::JumpIfFalse&&
            fn.code[pc+1].args.size()==1&&
            fn.code[pc+1].args[0]==*i.dest&&
            !jumpTargets.count(pc+1);
        const char* inverted=nullptr;
        if(i.text=="==")inverted="jne";
        else if(i.text=="!=")inverted="je";
        else if(i.text=="<")inverted=unsign?"jae":"jge";
        else if(i.text=="<=")inverted=unsign?"ja":"jg";
        else if(i.text==">")inverted=unsign?"jbe":"jle";
        else if(i.text==">=")inverted=unsign?"jb":"jl";
        if(singleUse&&adjacent&&inverted){
            load(i.args[0],"rax");load(i.args[1],"r10");
            out<<"    cmp rax, r10\n"
               <<"    "<<inverted<<" "<<pcLabel(fn,fn.code[pc+1].target)<<"\n";
            fusedBranches.insert(pc+1);
            break;
        }
        load(i.args[0],"rax");
        if(i.text=="/"||i.text=="%"){
            const auto it=constantValues.find(i.args[1]);
            const bool remainder=i.text=="%";
            const bool allowed=it!=constantValues.end()&&it->second!=0&&
                (unsign||it->second!=-1);
            if(allowed){
                const auto divisor=it->second;
                const auto magnitude=unsign?static_cast<std::uint64_t>(divisor):
                    (divisor<0?std::uint64_t{0}-static_cast<std::uint64_t>(divisor):
                     static_cast<std::uint64_t>(divisor));
                if(!unsign){
                    out<<"    mov r8, rax\n    sar r8, 63\n"
                       <<"    xor rax, r8\n    sub rax, r8\n";
                }
                emitUnsignedConstantDivision(out,magnitude,remainder,
                    localLabel(fn,pc,"div_done"));
                if(!unsign){
                    if(divisor<0&&!remainder)out<<"    not r8\n";
                    out<<"    xor rax, r8\n    sub rax, r8\n";
                }
            }else{
                load(i.args[1],"r10");
                if(unsign)out<<"    xor edx, edx\n    div r10\n";
                else out<<"    cqo\n    idiv r10\n";
                if(remainder)out<<"    mov rax, rdx\n";
            }
        }else{
            load(i.args[1],"r10");
            if(i.text=="+")out<<"    add rax, r10\n";
            else if(i.text=="-")out<<"    sub rax, r10\n";
            else if(i.text=="*")out<<"    imul rax, r10\n";
            else if(i.text=="&&"||i.text=="||"){out<<"    test rax, rax\n    setne al\n    movzx rax, al\n    test r10, r10\n    setne r10b\n    movzx r10, r10b\n"<<(i.text=="&&"?"    and rax, r10\n":"    or rax, r10\n");}else{out<<"    cmp rax, r10\n";if(i.text=="==")out<<"    sete al\n";else if(i.text=="!=")out<<"    setne al\n";else if(i.text=="<")out<<(unsign?"    setb al\n":"    setl al\n");else if(i.text=="<=")out<<(unsign?"    setbe al\n":"    setle al\n");else if(i.text==">")out<<(unsign?"    seta al\n":"    setg al\n");else if(i.text==">=")out<<(unsign?"    setae al\n":"    setge al\n");else{diagnostics.error("NQR-N5003",{},"unsupported native binary operator '"+i.text+"'");return false;}out<<"    movzx rax, al\n";}}
        if(i.text=="+"||i.text=="-"||i.text=="*"||
           i.text=="/"||i.text=="%")
            normalizeIntegerResult(out,i.type.kind);
        store(*i.dest,"rax");break;}
        case NirOp::Call:{if(i.text=="abi"||i.text=="host"){diagnostics.error("NQR-N5010",{},"generic named ABI dispatch is not yet lowered by native codegen");return false;}if(i.text=="textLength"){if(i.args.size()!=1){diagnostics.error("NQR-N5007",{},"native textLength accepts exactly one string");return false;}load(i.args[0],"rdi");out<<"    call noqeri_rt_text_length\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="print"){if(i.args.size()!=1){diagnostics.error("NQR-N5006",{},"native print accepts exactly one argument");return false;}Reg r=i.args[0];load(r,"rdi");auto k=r<l.regKinds.size()?l.regKinds[r]:ValueKind::Unknown;if(k==ValueKind::String)out<<"    call noqeri_rt_print_cstr\n";else if(k==ValueKind::Bool)out<<"    call noqeri_rt_print_bool\n";else if(k==ValueKind::Float){diagnostics.error("NQR-N5001",{},"float printing is not implemented yet");return false;}else out<<"    call noqeri_rt_print_int\n";if(i.dest){out<<"    xor eax, eax\n";store(*i.dest,"rax");}}else if(i.text=="clockMillis"){out<<"    call noqeri_rt_clock_millis\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="platform"){out<<"    call noqeri_rt_platform\n";if(i.dest)store(*i.dest,"rax");}else{if(i.args.size()>6){diagnostics.error("NQR-N5004",{},"native calls currently support at most 6 arguments");return false;}for(std::size_t a=0;a<i.args.size();++a)load(i.args[a],args[a]);out<<"    call "<<symbolForCall(p,i.text)<<"\n";if(i.dest)store(*i.dest,"rax");}break;}
        case NirOp::Jump:out<<"    jmp "<<pcLabel(fn,i.target)<<"\n";break;
        case NirOp::JumpIfFalse:load(i.args[0],"rax");out<<"    test rax, rax\n    jz "<<pcLabel(fn,i.target)<<"\n";break;
        case NirOp::Return:if(!i.args.empty())load(i.args[0],"rax");else out<<"    xor eax, eax\n";restoreSaved();out<<"    leave\n    ret\n";break;
        case NirOp::Nop:break;
    }}return true;
}
}

bool NativeBackend::emitAssembly(const NirProgram&p,const std::filesystem::path&output,Diagnostics&diagnostics)const{
    if(!p.entry.partitions.empty()){
        diagnostics.error("NQR-PGAS301",{},"native backend does not yet lower @partition arrays to remote addresses; refusing local execution");
        return false;
    }
    for(const auto&fn:p.functions)if(!fn.partitions.empty()){
        diagnostics.error("NQR-PGAS301",{},"native backend does not yet lower @partition arrays to remote addresses; refusing local execution");
        return false;
    }
    std::ofstream out(output,std::ios::trunc);if(!out){diagnostics.error("NQR-N5000",{},"cannot write assembly output: "+output.string());return false;}
    std::map<std::string,std::string>strings;std::size_t sid=0;auto collect=[&](const NirFunction&f){for(const auto&i:f.code)if(i.op==NirOp::Const)if(auto s=std::get_if<std::string>(&i.literal))if(!strings.count(*s))strings[*s]=".L_noqeri_str_"+std::to_string(sid++);};collect(p.entry);for(const auto&f:p.functions)collect(f);
    std::unordered_map<std::string,ValueKind>returns;for(int pass=0;pass<3;++pass)for(const auto&f:p.functions)if(!f.isExtern)returns[f.name]=inferReturn(f,returns);
    out<<".intel_syntax noprefix\n.text\n";emitRuntime(out);if(!emitFunction(out,p,p.entry,returns,strings,diagnostics))return false;for(const auto&f:p.functions)if(!emitFunction(out,p,f,returns,strings,diagnostics))return false;
    out<<"\n.global noqeri_entry\nnoqeri_entry:\n    mov QWORD PTR [rip + noqeri_abi_current], rdi\n    sub rsp, 8\n    call "<<internalSymbol("__entry")<<"\n    add rsp, 8\n    ret\n";
    out<<"\n.data\n.L_noqeri_newline: .byte 10\n.L_noqeri_true: .asciz \"true\"\n.L_noqeri_false_text: .asciz \"false\"\n.L_noqeri_standalone: .asciz \"freestanding\"\n";
    for(const auto&[value,label]:strings){out<<label<<": .byte ";for(std::size_t i=0;i<value.size();++i){if(i)out<<", ";out<<static_cast<unsigned int>(static_cast<unsigned char>(value[i]));}if(!value.empty())out<<", ";out<<"0\n";}
    // ELF x86-64 native objects must not request an executable stack.
    out<<"\n.section .note.GNU-stack,\"\",@progbits\n";
    return !diagnostics.hasErrors();
}
} // namespace noe
