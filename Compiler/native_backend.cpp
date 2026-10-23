#include "noe.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace noe {
namespace {

enum class ValueKind{Unknown,Int,Bool,String,Float,Null};
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
void emitLoadWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    movzx eax, BYTE PTR [rax]\n";else if(width==2)out<<"    movzx eax, WORD PTR [rax]\n";else if(width==4)out<<"    mov eax, DWORD PTR [rax]\n";else out<<"    mov rax, QWORD PTR [rax]\n";}
void emitStoreWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    mov BYTE PTR [rax], r10b\n";else if(width==2)out<<"    mov WORD PTR [rax], r10w\n";else if(width==4)out<<"    mov DWORD PTR [rax], r10d\n";else out<<"    mov QWORD PTR [rax], r10\n";}
void emitExchangeWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    xchg BYTE PTR [rax], r10b\n    movzx eax, r10b\n";else if(width==2)out<<"    xchg WORD PTR [rax], r10w\n    movzx eax, r10w\n";else if(width==4)out<<"    xchg DWORD PTR [rax], r10d\n    mov eax, r10d\n";else out<<"    xchg QWORD PTR [rax], r10\n    mov rax, r10\n";}
void emitAtomicStoreWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    xchg BYTE PTR [rax], r10b\n";else if(width==2)out<<"    xchg WORD PTR [rax], r10w\n";else if(width==4)out<<"    xchg DWORD PTR [rax], r10d\n";else out<<"    xchg QWORD PTR [rax], r10\n";}
void emitCompareExchangeWidth(std::ostream&out,std::size_t width){if(width==1)out<<"    lock cmpxchg BYTE PTR [r10], cl\n    movzx eax, al\n";else if(width==2)out<<"    lock cmpxchg WORD PTR [r10], cx\n    movzx eax, ax\n";else if(width==4)out<<"    lock cmpxchg DWORD PTR [r10], ecx\n    mov eax, eax\n";else out<<"    lock cmpxchg QWORD PTR [r10], rcx\n";}
void emitCast(std::ostream&out,const std::string&t){if(t=="u8")out<<"    and rax, 255\n";else if(t=="u16")out<<"    and rax, 65535\n";else if(t=="u32")out<<"    mov eax, eax\n";else if(t=="i8")out<<"    movsx rax, al\n";else if(t=="i16")out<<"    movsx rax, ax\n";else if(t=="i32")out<<"    movsxd rax, eax\n";}
bool safeInlineAsm(const std::string& text){if(text.empty())return false;if(text.find('\n')!=std::string::npos||text.find('\r')!=std::string::npos||text.find(':')!=std::string::npos)return false;auto p=text.find_first_not_of(" \t");return p!=std::string::npos&&text[p]!='.';}

bool emitFunction(std::ostream&out,const NirProgram&p,const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns,const std::map<std::string,std::string>&strings,Diagnostics&diagnostics){
    if(fn.isExtern)return true;
    auto l=layoutFor(fn,returns);const char*args[]={"rdi","rsi","rdx","rcx","r8","r9"};
    // On System V x86-64 the first six integral/pointer arguments are in
    // registers; additional arguments are read from the caller's stack.
    if(fn.params.size()>32){diagnostics.error("NQR-N5004",{},"native functions support at most 32 scalar arguments");return false;}
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
    std::size_t frame=align16(frameBytes);
    out<<"\n"<<symbol<<":\n    push rbp\n    mov rbp, rsp\n";if(frame)out<<"    sub rsp, "<<frame<<"\n";
    for(std::size_t i=0;i<fn.params.size();++i){
        if(i<6)out<<"    mov "<<varSlot(fn,l,fn.params[i])<<", "<<args[i]<<"\n";
        else {
            const auto stackOffset=16+(i-6)*8;
            out<<"    mov rax, QWORD PTR [rbp+"<<stackOffset<<"]\n";
            out<<"    mov "<<varSlot(fn,l,fn.params[i])<<", rax\n";
        }
    }
    auto load=[&](Reg r,const char*cpu){out<<"    mov "<<cpu<<", "<<regSlot(r)<<"\n";};auto store=[&](Reg r,const char*cpu){out<<"    mov "<<regSlot(r)<<", "<<cpu<<"\n";};
    for(std::size_t pc=0;pc<fn.code.size();++pc){const auto&i=fn.code[pc];out<<pcLabel(fn,pc)<<":\n";switch(i.op){
        case NirOp::Const:{if(!i.dest)break;if(auto v=std::get_if<std::int64_t>(&i.literal))out<<"    mov rax, "<<*v<<"\n";else if(auto v=std::get_if<bool>(&i.literal))out<<"    mov rax, "<<(*v?1:0)<<"\n";else if(auto v=std::get_if<std::string>(&i.literal)){auto s=strings.find(*v);if(s==strings.end()){diagnostics.error("NQR-N5005",{},"internal string-pool error");return false;}out<<"    lea rax, [rip + "<<s->second<<"]\n";}else if(std::holds_alternative<std::monostate>(i.literal))out<<"    xor eax, eax\n";else{diagnostics.error("NQR-N5001",{},"float native code generation is not implemented yet");return false;}store(*i.dest,"rax");break;}
        case NirOp::Load:if(i.dest){out<<"    mov rax, "<<varSlot(fn,l,i.text)<<"\n";store(*i.dest,"rax");}break;
        case NirOp::Store:if(!i.args.empty()){load(i.args[0],"rax");out<<"    mov "<<varSlot(fn,l,i.text)<<", rax\n";}break;
        case NirOp::AddressOf:if(i.dest){out<<"    lea rax, "<<varAddress(fn,l,i.text)<<"\n";store(*i.dest,"rax");}break;
        case NirOp::LoadMemory:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");emitLoadWidth(out,i.width);store(*i.dest,"rax");}break;
        case NirOp::StoreMemory:if(i.args.size()==2){load(i.args[0],"rax");load(i.args[1],"r10");emitStoreWidth(out,i.width);}break;
        case NirOp::PtrOffset:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");if(i.args.size()>1){load(i.args[1],"r10");if(i.width!=1)out<<"    imul r10, "<<i.width<<"\n";out<<"    add rax, r10\n";}else if(i.target)out<<"    add rax, "<<i.target<<"\n";store(*i.dest,"rax");}break;
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
            out<<"    test rax, rax\n    js "<<failed<<"\n";
            load(i.args[1],"r10");
            out<<"    cmp rax, r10\n    jb "<<valid<<"\n"<<failed<<":\n    ud2\n"<<valid<<":\n";
            break;
        }
        case NirOp::AtomicLoad:if(i.dest&&!i.args.empty()){out<<"    mfence\n";load(i.args[0],"rax");emitLoadWidth(out,i.width);out<<"    mfence\n";store(*i.dest,"rax");}break;
        case NirOp::AtomicStore:if(i.args.size()==2){load(i.args[0],"rax");load(i.args[1],"r10");emitAtomicStoreWidth(out,i.width);}break;
        case NirOp::AtomicExchange:if(i.dest&&i.args.size()==2){load(i.args[0],"rax");load(i.args[1],"r10");emitExchangeWidth(out,i.width);store(*i.dest,"rax");}break;
        case NirOp::AtomicCompareExchange:if(i.dest&&i.args.size()==3){load(i.args[0],"r10");load(i.args[1],"rax");load(i.args[2],"rcx");emitCompareExchangeWidth(out,i.width);store(*i.dest,"rax");}break;
        case NirOp::AtomicFence:out<<"    mfence\n";break;
        case NirOp::Intrinsic:{if(i.text=="x86.pause")out<<"    pause\n    xor eax, eax\n";else if(i.text=="x86.rdtsc")out<<"    rdtsc\n    shl rdx, 32\n    or rax, rdx\n";else if(i.text=="x86.halt")out<<"    hlt\n    xor eax, eax\n";else if(i.text=="compiler.fence")out<<"    mfence\n    xor eax, eax\n";else{diagnostics.error("NQR-N5020",{},"unsupported native intrinsic '"+i.text+"'");return false;}if(i.dest)store(*i.dest,"rax");break;}
        case NirOp::InlineAsm:if(!safeInlineAsm(i.text)){diagnostics.error("NQR-N5021",{},"inline asm must be one instruction and may not contain labels or assembler directives");return false;}out<<"    "<<i.text<<"\n";if(i.dest){out<<"    xor eax, eax\n";store(*i.dest,"rax");}break;
        case NirOp::Try:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");auto ok=localLabel(fn,pc,"try_ok");out<<"    test rax, rax\n    jns "<<ok<<"\n    leave\n    ret\n"<<ok<<":\n";store(*i.dest,"rax");}break;
        case NirOp::Throw:if(!i.args.empty()){load(i.args[0],"rax");auto encoded=localLabel(fn,pc,"throw_encoded");out<<"    test rax, rax\n    js "<<encoded<<"\n    neg rax\n    sub rax, 1\n"<<encoded<<":\n    leave\n    ret\n";}break;
        case NirOp::Cast:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");emitCast(out,i.text);store(*i.dest,"rax");}break;
        case NirOp::Unary:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");if(i.text=="-")out<<"    neg rax\n";else if(i.text=="!")out<<"    test rax, rax\n    sete al\n    movzx rax, al\n";store(*i.dest,"rax");}break;
        case NirOp::Binary:{if(!i.dest||i.args.size()!=2)break;auto a=i.args[0]<l.regKinds.size()?l.regKinds[i.args[0]]:ValueKind::Unknown,b=i.args[1]<l.regKinds.size()?l.regKinds[i.args[1]]:ValueKind::Unknown;if(a==ValueKind::Float||b==ValueKind::Float){diagnostics.error("NQR-N5001",{},"float native arithmetic is not implemented yet");return false;}if(a==ValueKind::String||b==ValueKind::String){diagnostics.error("NQR-N5002",{},"runtime string binary operations are not implemented yet");return false;}load(i.args[0],"rax");load(i.args[1],"r10");if(i.text=="+")out<<"    add rax, r10\n";else if(i.text=="-")out<<"    sub rax, r10\n";else if(i.text=="*")out<<"    imul rax, r10\n";else if(i.text=="/"||i.text=="%"){out<<"    cqo\n    idiv r10\n";if(i.text=="%")out<<"    mov rax, rdx\n";}else if(i.text=="&&"||i.text=="||"){out<<"    test rax, rax\n    setne al\n    movzx rax, al\n    test r10, r10\n    setne r10b\n    movzx r10, r10b\n"<<(i.text=="&&"?"    and rax, r10\n":"    or rax, r10\n");}else{out<<"    cmp rax, r10\n";if(i.text=="==")out<<"    sete al\n";else if(i.text=="!=")out<<"    setne al\n";else if(i.text=="<")out<<"    setl al\n";else if(i.text=="<=")out<<"    setle al\n";else if(i.text==">")out<<"    setg al\n";else if(i.text==">=")out<<"    setge al\n";else{diagnostics.error("NQR-N5003",{},"unsupported native binary operator '"+i.text+"'");return false;}out<<"    movzx rax, al\n";}store(*i.dest,"rax");break;}
        case NirOp::Call:{if(i.text=="abi"||i.text=="host"){diagnostics.error("NQR-N5010",{},"generic named ABI dispatch is not yet lowered by native codegen");return false;}if(i.text=="textLength"){if(i.args.size()!=1){diagnostics.error("NQR-N5007",{},"native textLength accepts exactly one string");return false;}load(i.args[0],"rdi");out<<"    call noqeri_rt_text_length\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="print"){if(i.args.size()!=1){diagnostics.error("NQR-N5006",{},"native print accepts exactly one argument");return false;}Reg r=i.args[0];load(r,"rdi");auto k=r<l.regKinds.size()?l.regKinds[r]:ValueKind::Unknown;if(k==ValueKind::String)out<<"    call noqeri_rt_print_cstr\n";else if(k==ValueKind::Bool)out<<"    call noqeri_rt_print_bool\n";else if(k==ValueKind::Float){diagnostics.error("NQR-N5001",{},"float printing is not implemented yet");return false;}else out<<"    call noqeri_rt_print_int\n";if(i.dest){out<<"    xor eax, eax\n";store(*i.dest,"rax");}}else if(i.text=="clockMillis"){out<<"    call noqeri_rt_clock_millis\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="platform"){out<<"    call noqeri_rt_platform\n";if(i.dest)store(*i.dest,"rax");}else{
            if(i.args.size()>32){diagnostics.error("NQR-N5004",{},"native calls support at most 32 scalar arguments");return false;}
            const std::size_t stackArgs=i.args.size()>6?i.args.size()-6:0;
            const std::size_t padding=stackArgs%2;
            // Reserve padding BEFORE pushing stack args so the first stack
            // argument stays immediately above the return address. Every
            // outgoing call observes 16-byte stack alignment.
            if(padding)out<<"    sub rsp, 8\n";
            for(std::size_t a=i.args.size();a>6;--a){
                load(i.args[a-1],"rax");
                out<<"    push rax\n";
            }
            for(std::size_t a=0;a<std::min<std::size_t>(i.args.size(),6);++a)
                load(i.args[a],args[a]);
            out<<"    call "<<symbolForCall(p,i.text)<<"\n";
            if(stackArgs+padding)out<<"    add rsp, "<<(stackArgs+padding)*8<<"\n";
            if(i.dest)store(*i.dest,"rax");
        }break;}
        case NirOp::Jump:out<<"    jmp "<<pcLabel(fn,i.target)<<"\n";break;
        case NirOp::JumpIfFalse:load(i.args[0],"rax");out<<"    test rax, rax\n    jz "<<pcLabel(fn,i.target)<<"\n";break;
        case NirOp::Return:if(!i.args.empty())load(i.args[0],"rax");else out<<"    xor eax, eax\n";out<<"    leave\n    ret\n";break;
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
    return !diagnostics.hasErrors();
}
} // namespace noe
