#include "noe.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
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
// Compute the branch-free unsigned division multiplier using only uint64_t
// arithmetic, so cross-compiling the Stage-0 compiler does not require a
// vendor-specific 128-bit integer type. This is the libdivide-style unsigned
// magic-number construction. For a non-power-of-two d, 2^k < d < 2^(k+1).
struct UnsignedDivisionMagic {
    std::uint64_t multiplier=0;
    unsigned shift=0;
    bool add=false;
};
UnsignedDivisionMagic unsignedDivisionMagic(std::uint64_t divisor){
    unsigned shift=0;
    for(auto value=divisor;value>1;value>>=1)++shift;
    const auto max=std::numeric_limits<std::uint64_t>::max();
    // Start with floor(2^64/d) and the exact remainder; 2^64 is
    // represented as UINT64_MAX + 1 without overflowing unsigned math.
    std::uint64_t quotient=max/divisor;
    std::uint64_t remainder=max%divisor;
    if(remainder==divisor-1){++quotient;remainder=0;}
    else ++remainder;
    // Long-divide 2^(64+shift) by d. Every intermediate quotient fits
    // u64 since d > 2^shift. Avoid overflowing 2*remainder as well.
    for(unsigned bit=0;bit<shift;++bit){
        const bool carry=remainder>=divisor-remainder;
        quotient=(quotient<<1)+static_cast<std::uint64_t>(carry);
        remainder=carry?remainder-(divisor-remainder):remainder*2;
    }
    const bool add=(divisor-remainder)>=(std::uint64_t{1}<<shift);
    if(add){
        const bool carry=remainder>=divisor-remainder;
        quotient=(quotient<<1)+static_cast<std::uint64_t>(carry);
    }
    // Overflow on +1 is intentional: the multiplier is modulo 2^64.
    return {quotient+1,shift,add};
}
// Exact u64 quotient/remainder for an immediate divisor. A magic multiply
// and shift (plus an optional branchless adjustment) replace the previous
// reciprocal correction branch. Signed lowering handles signs separately.
// Division by zero and signed MIN/-1 remain on the trapping IDIV path.
void emitUnsignedConstantDivision(std::ostream& out,std::uint64_t divisor,
                                 bool remainder){
    if(divisor==1){
        if(remainder)out<<"    xor eax, eax\n";
        return;
    }
    if((divisor&(divisor-1))==0){
        unsigned shift=0;
        for(auto value=divisor;value>1;value>>=1)++shift;
        if(remainder){
            if(divisor-1<=static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max()))
                out<<"    and rax, "<<(divisor-1)<<"\n";
            else out<<"    mov r10, "<<(divisor-1)<<"\n    and rax, r10\n";
        }else out<<"    shr rax, "<<shift<<"\n";
        return;
    }
    const auto magic=unsignedDivisionMagic(divisor);
    if(remainder||magic.add)out<<"    mov r11, rax\n";
    out<<"    mov r10, "<<magic.multiplier<<"\n"
       <<"    mul r10\n"
       <<"    mov rax, rdx\n";
    if(magic.add){
        // q = (mulhi(n, magic) + ((n - mulhi(n, magic)) >> 1)) >> shift.
        // r11 still contains the original dividend for a remainder.
        out<<"    mov r10, r11\n"
           <<"    sub r10, rax\n"
           <<"    shr r10, 1\n"
           <<"    add rax, r10\n";
    }
    if(magic.shift)out<<"    shr rax, "<<magic.shift<<"\n";
    if(remainder){
        // n - q*d: low 64 bits suffice, and exact q*d never exceeds n.
        if(divisor<=static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            out<<"    imul rax, rax, "<<divisor<<"\n";
        else
            out<<"    mov r10, "<<divisor<<"\n    imul rax, r10\n";
        out<<"    sub r11, rax\n"
           <<"    mov rax, r11\n";
    }
}

ValueKind literalKind(const NirValue&v){if(std::holds_alternative<std::int64_t>(v))return ValueKind::Int;if(std::holds_alternative<bool>(v))return ValueKind::Bool;if(std::holds_alternative<std::string>(v))return ValueKind::String;if(std::holds_alternative<double>(v))return ValueKind::Float;return ValueKind::Null;}
bool isComparison(const std::string&op){return op=="=="||op=="!="||op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||";}
std::string safe(const std::string&name){std::string out;out.reserve(name.size());for(char c:name)out+=(std::isalnum(static_cast<unsigned char>(c))||c=='_')?c:'_';return out;}
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
        else if(i.op==NirOp::LoadMemory&&i.dest)
            l.regKinds[*i.dest]=i.type.kind==TypeKind::Float?ValueKind::Float:
                i.type.kind==TypeKind::Bool?ValueKind::Bool:ValueKind::Int;
        else if(i.op==NirOp::Cast&&i.dest)
            l.regKinds[*i.dest]=i.type.kind==TypeKind::Float?ValueKind::Float:ValueKind::Int;
        else if((i.op==NirOp::AddressOf||i.op==NirOp::PtrOffset||i.op==NirOp::StackAlloc||i.op==NirOp::MakeSlice||i.op==NirOp::SliceData||i.op==NirOp::SliceLen||i.op==NirOp::AtomicLoad||i.op==NirOp::AtomicExchange||i.op==NirOp::AtomicCompareExchange||i.op==NirOp::Try||i.op==NirOp::Intrinsic)&&i.dest)l.regKinds[*i.dest]=ValueKind::Int;
        else if(i.op==NirOp::InlineAsm&&i.dest)l.regKinds[*i.dest]=ValueKind::Null;
        else if(i.op==NirOp::Binary&&i.dest&&i.args.size()==2){if(isComparison(i.text))l.regKinds[*i.dest]=ValueKind::Bool;else{auto a=rk(i.args[0]),b=rk(i.args[1]);if(a==ValueKind::String||b==ValueKind::String)l.regKinds[*i.dest]=ValueKind::String;else if(a==ValueKind::Float||b==ValueKind::Float)l.regKinds[*i.dest]=ValueKind::Float;else l.regKinds[*i.dest]=ValueKind::Int;}}
        else if(i.op==NirOp::Call&&i.dest){if((i.text=="nq_list_at"||i.text=="nq_list_pop"||i.text=="nq_list_set")&&i.type.kind==TypeKind::Float)l.regKinds[*i.dest]=ValueKind::Float;else if(i.text=="print")l.regKinds[*i.dest]=ValueKind::Null;else if(i.text=="clockMillis"||i.text=="textLength")l.regKinds[*i.dest]=ValueKind::Int;else if(i.text=="platform"||i.text.rfind("nq_internal_interp_",0)==0)l.regKinds[*i.dest]=ValueKind::String;else{auto it=returns.find(i.text);l.regKinds[*i.dest]=it==returns.end()?ValueKind::Unknown:it->second;}}
    }
    return l;
}
ValueKind inferReturn(const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns){auto l=layoutFor(fn,returns);ValueKind result=ValueKind::Null;for(const auto&i:fn.code)if(i.op==NirOp::Return&&!i.args.empty()&&i.args[0]<l.regKinds.size()){auto k=l.regKinds[i.args[0]];if(result==ValueKind::Null||result==ValueKind::Unknown)result=k;else if(k!=ValueKind::Unknown&&result!=k)result=ValueKind::Unknown;}return result;}
std::string symbolForCall(const std::unordered_map<std::string,std::string>& symbols,const std::string& name){
    const auto it=symbols.find(name);
    return it==symbols.end()?safe(name):it->second;
}

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
// Hosted x86-64 native float output. The ABI still controls writes;
// libc formats the IEEE-754 double with the same six fractional digits as
// the Stage-0 interpreter's std::to_string(double).
noqeri_rt_print_float:
    push rbp
    mov rbp, rsp
    sub rsp, 544
    mov rax, rdi
    lea rdi, [rbp-528]
    mov esi, 512
    lea rdx, [rip + .L_noqeri_float_format]
    movq xmm0, rax
    mov al, 1
    call snprintf
    test eax, eax
    js .L_noqeri_float_print_error
    cmp eax, 512
    jge .L_noqeri_float_print_error
    mov QWORD PTR [rbp-536], rax
    lea rdi, [rbp-528]
    mov rsi, QWORD PTR [rbp-536]
    call noqeri_rt_write
    lea rdi, [rip + .L_noqeri_newline]
    mov esi, 1
    call noqeri_rt_write
    leave
    ret
.L_noqeri_float_print_error:
    ud2
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
noqeri_rt_bounds_trap:
    lea rdi, [rip + .L_noqeri_bounds_err]
    mov rsi, 46
    call noqeri_rt_write
    cmp eax, -1
    jne .L_noqeri_bounds_exit
    mov rax, 1
    mov rdi, 2
    lea rsi, [rip + .L_noqeri_bounds_err]
    mov rdx, 46
    syscall
.L_noqeri_bounds_exit:
    mov edi, 1
    call exit
noqeri_rt_has_avx2:
    mov al, BYTE PTR [rip + .L_noqeri_avx2_state]
    test al, al
    jnz .L_noqeri_avx2_cached
    push rbx
    push rdx
    mov eax, 1
    cpuid
    and ecx, 402653184
    cmp ecx, 402653184
    jne .L_noqeri_no_avx2
    xor ecx, ecx
    xgetbv
    and eax, 6
    cmp eax, 6
    jne .L_noqeri_no_avx2
    mov eax, 7
    xor ecx, ecx
    cpuid
    test ebx, 32
    jz .L_noqeri_no_avx2
    pop rdx
    pop rbx
    mov BYTE PTR [rip + .L_noqeri_avx2_state], 1
    mov eax, 1
    ret
.L_noqeri_no_avx2:
    pop rdx
    pop rbx
    mov BYTE PTR [rip + .L_noqeri_avx2_state], 2
    mov eax, 2
    ret
.L_noqeri_avx2_cached:
    movzx eax, al
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

bool emitFunction(std::ostream&out,const NirFunction&fn,const std::unordered_map<std::string,ValueKind>&returns,const std::map<std::string,std::string>&strings,const std::unordered_map<std::string,std::string>& symbols,Diagnostics&diagnostics){
    if(fn.isExtern)return true;
    auto l=layoutFor(fn,returns);const char*args[]={"rdi","rsi","rdx","rcx","r8","r9"};
    // Physically allocate NIR registers while reserving volatile rax/r10/r11
    // for instruction lowering. Disable allocation around unmodelled inline-asm
    // clobbers, until inline-asm constraints are implemented.
    // Constant divisors are discovered at emission time. Casted positive
    // integer literals remain constants even when optimization has not
    // folded the Cast instruction itself.
    std::unordered_map<Reg,std::int64_t> constantValues;
    constantValues.reserve(fn.nextReg/8+8);
    // NIR virtual registers are densely numbered. Counting definitions in a
    // vector avoids a hash insertion for every instruction emitted.
    std::vector<unsigned> definitions(fn.nextReg,0);
    for(const auto& in:fn.code)
        if(in.dest&&*in.dest<definitions.size())++definitions[*in.dest];
    for(const auto& in:fn.code){
        if(!in.dest||*in.dest>=definitions.size()||definitions[*in.dest]!=1)continue;
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
    std::vector<bool> nonNegative(fn.nextReg, false);
    std::unordered_map<std::string, bool> nonNegativeVars;
    for(std::size_t r=0; r<fn.registerTypes.size() && r<fn.nextReg; ++r){
        if(isUnsigned(fn.registerTypes[r].kind)) nonNegative[r] = true;
    }
    for(const auto& [reg, val] : constantValues){
        if(val >= 0 && reg < nonNegative.size()) nonNegative[reg] = true;
    }
    for(int pass=0; pass<4; ++pass){
        bool updated = false;
        for(const auto& in : fn.code){
            if(in.op == NirOp::Store && !in.args.empty() && in.args[0] < nonNegative.size()){
                auto it = nonNegativeVars.find(in.text);
                if(it == nonNegativeVars.end()){
                    nonNegativeVars[in.text] = nonNegative[in.args[0]];
                } else if(!nonNegative[in.args[0]]){
                    it->second = false;
                }
            }
            if(!in.dest || *in.dest >= nonNegative.size() || nonNegative[*in.dest]) continue;
            bool isNonNeg = false;
            if(in.op == NirOp::Const){
                if(auto v = std::get_if<std::int64_t>(&in.literal)) isNonNeg = (*v >= 0);
                else if(std::holds_alternative<bool>(in.literal)) isNonNeg = true;
            }else if(in.op == NirOp::Load){
                auto it = nonNegativeVars.find(in.text);
                if(it != nonNegativeVars.end() && it->second) isNonNeg = true;
            }else if(in.op == NirOp::SliceLen || in.op == NirOp::StackAlloc ||
                     in.op == NirOp::MakeSlice || in.op == NirOp::AddressOf){
                isNonNeg = true;
            }else if(in.op == NirOp::Cast && in.args.size() == 1){
                if(isUnsigned(in.type.kind) ||
                   (in.args[0] < nonNegative.size() && nonNegative[in.args[0]]))
                    isNonNeg = true;
            }else if(in.op == NirOp::Binary && in.args.size() == 2){
                if(isComparison(in.text)){
                    isNonNeg = true;
                }else if(in.text == "+" || in.text == "*"){
                    const bool a0 = (in.args[0] < nonNegative.size() && nonNegative[in.args[0]]) ||
                                    (constantValues.count(in.args[0]) && constantValues.at(in.args[0]) >= 0);
                    const bool a1 = (in.args[1] < nonNegative.size() && nonNegative[in.args[1]]) ||
                                    (constantValues.count(in.args[1]) && constantValues.at(in.args[1]) >= 0);
                    if(a0 && a1) isNonNeg = true;
                }else if(in.text == "/" || in.text == "%"){
                    const bool a0 = (in.args[0] < nonNegative.size() && nonNegative[in.args[0]]) ||
                                    (constantValues.count(in.args[0]) && constantValues.at(in.args[0]) >= 0);
                    const bool a1 = (constantValues.count(in.args[1]) && constantValues.at(in.args[1]) > 0);
                    if(a0 && (in.text == "%" || a1)) isNonNeg = true;
                }else if(in.text == "&"){
                    const bool a0 = (in.args[0] < nonNegative.size() && nonNegative[in.args[0]]) ||
                                    (constantValues.count(in.args[0]) && constantValues.at(in.args[0]) >= 0);
                    const bool a1 = (in.args[1] < nonNegative.size() && nonNegative[in.args[1]]) ||
                                    (constantValues.count(in.args[1]) && constantValues.at(in.args[1]) >= 0);
                    if(a0 || a1) isNonNeg = true;
                }else if(in.text == ">>"){
                    if((in.args[0] < nonNegative.size() && nonNegative[in.args[0]]) ||
                       (constantValues.count(in.args[0]) && constantValues.at(in.args[0]) >= 0))
                        isNonNeg = true;
                }
            }
            if(isNonNeg){
                nonNegative[*in.dest] = true;
                updated = true;
            }
        }
        if(!updated) break;
    }
    std::unordered_map<Reg,std::string> physical;
    std::vector<std::string> saved;
    const bool hasInlineAsm=std::any_of(fn.code.begin(),fn.code.end(),
        [](const NirInstruction& i){return i.op==NirOp::InlineAsm;});
    // Keep the two hottest non-address-taken named locals in callee-saved
    // registers. Repeated loop-carried loads/stores otherwise round-trip
    // through the frame even when the values fit in machine registers.
    // Remove claimed registers from the virtual-register allocator's pool.
    std::unordered_map<std::string,std::string> variablePhysical;
    if(!hasInlineAsm&&!fn.code.empty()){
        std::unordered_set<std::string> addressTaken,initialized;
        for(const auto& name:fn.params)initialized.insert(name);
        for(const auto& in:fn.code){
            if(in.op==NirOp::AddressOf)addressTaken.insert(in.text);
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
        std::vector<std::pair<std::string,std::size_t>> ranked(
            hot.begin(),hot.end());
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
            return a.second>b.second||
                   (a.second==b.second&&a.first<b.first);
        });
        const bool hasCalls = std::any_of(fn.code.begin(),fn.code.end(),
            [](const NirInstruction& in){return in.op==NirOp::Call||in.op==NirOp::Intrinsic;});
        const char* hotRegisters[]={"rbx","r12","r13","r14","r15"};
        const std::size_t maxHotVars = hasCalls ? 1u : 2u;
        for(std::size_t n=0;n<ranked.size()&&n<maxHotVars;++n){
            if(ranked[n].second<2u)break;
            variablePhysical.emplace(ranked[n].first,hotRegisters[n]);
            saved.emplace_back(hotRegisters[n]);
        }
    }
    if(!hasInlineAsm){
        std::vector<std::string> pool;
        const bool hasCalls = std::any_of(fn.code.begin(),fn.code.end(),
            [](const NirInstruction& in){return in.op==NirOp::Call||in.op==NirOp::Intrinsic;});
        const bool hasCmpXchg = std::any_of(fn.code.begin(),fn.code.end(),
            [](const NirInstruction& in){return in.op==NirOp::AtomicCompareExchange;});
        bool mayUseR8 = false;
        bool mayUseRdx = false;
        bool mayUseRcx = hasCmpXchg;
        {
            bool hasStackAlloc = false;
            bool hasBackJump = false;
            for(std::size_t idx=0;idx<fn.code.size();++idx){
                const auto& in=fn.code[idx];
                if(in.op==NirOp::StackAlloc)hasStackAlloc=true;
                if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&in.target<=idx)hasBackJump=true;
            }
            if(hasStackAlloc&&hasBackJump){
                mayUseR8=true;
                mayUseRcx=true;
            }
        }
        for(std::size_t idx=0;idx<fn.code.size();++idx){
            const auto& in=fn.code[idx];
            if(in.op==NirOp::LoadMemory&&in.width==8&&!in.isVolatile){
                // Conservative check matching memoryComparisons window
                for(std::size_t k=idx+1;k<fn.code.size()&&k<=idx+5;++k){
                    const auto& c=fn.code[k];
                    if(c.op==NirOp::Binary&&!c.args.empty()&&in.dest&&c.args[0]==*in.dest){
                        mayUseR8=true;break;
                    }
                }
            }
            if(in.op==NirOp::Intrinsic&&in.text=="x86.rdtsc")mayUseRdx=true;
            if(in.op==NirOp::Binary&&in.args.size()==2){
                const auto ak=in.args[0]<l.regKinds.size()?l.regKinds[in.args[0]]:ValueKind::Unknown;
                const auto bk=in.args[1]<l.regKinds.size()?l.regKinds[in.args[1]]:ValueKind::Unknown;
                if((ak==ValueKind::Float||bk==ValueKind::Float)&&isComparison(in.text))
                    mayUseRdx=true;
                if(in.text=="/"||in.text=="%"){
                    const auto opK=in.args[0]<fn.registerTypes.size()?fn.registerTypes[in.args[0]].kind:TypeKind::Unknown;
                    const bool isUn=isUnsigned(opK)||(in.args[0]<nonNegative.size()&&nonNegative[in.args[0]]);
                    if(!isUn)mayUseR8=true;
                    const auto cit=constantValues.find(in.args[1]);
                    bool isPow2=false;
                    if(cit!=constantValues.end()&&cit->second>0){
                        const auto d=static_cast<std::uint64_t>(cit->second);
                        if((d&(d-1))==0)isPow2=true;
                    }
                    if(!isPow2)mayUseRdx=true;
                }
            }
        }
        if(!hasCalls){
            if(!mayUseRdx)pool.push_back("rdx");
            if(!mayUseR8)pool.push_back("r8");
            if(!mayUseRcx)pool.push_back("rcx");
            pool.push_back("r9");
            pool.push_back("rdi");
            pool.push_back("rsi");
        }
        if(std::none_of(variablePhysical.begin(),variablePhysical.end(),
                [](const auto& entry){return entry.second=="rbx";}))
            pool.push_back("rbx");
        for(const char* reg:{"r12","r13","r14","r15"}){
            if(std::none_of(variablePhysical.begin(),variablePhysical.end(),
                    [&](const auto& entry){return entry.second==reg;}))
                pool.push_back(reg);
        }
        const auto plan=LinearScanRegisterAllocator{}.allocate(fn,pool);
        for(const auto& loc:plan.locations)
            if(!loc.spilled&&!loc.physical.empty())physical.emplace(loc.value,loc.physical);
        for(const auto* reg:{"r12","r13","r14","r15","rbx","rsi","rdi"})
            if(std::any_of(physical.begin(),physical.end(),
                [&](const auto& entry){return entry.second==reg;}))saved.emplace_back(reg);
    }
    if(fn.params.size()>6){diagnostics.error("NQR-N5004",{},"Noqeri x86-64 calling convention currently supports at most 6 parameters");return false;}
    std::string symbol=fn.isExport?safe(fn.name):internalSymbol(fn.name);
    if(fn.isExport)out<<"\n.global "<<symbol<<"\n";
    std::vector<std::size_t> spillOffsets(fn.nextReg,0);
    std::size_t frameSlots=0;
    auto assignSpillSlot=[&](Reg r)->std::size_t{
        if(r>=spillOffsets.size())spillOffsets.resize(static_cast<std::size_t>(r)+1,0);
        if(spillOffsets[r]==0)spillOffsets[r]=(++frameSlots)*8;
        return spillOffsets[r];
    };
    for(const auto& in:fn.code){
        if(in.dest&&!physical.count(*in.dest))assignSpillSlot(*in.dest);
        for(Reg arg:in.args)if(!physical.count(arg))assignSpillSlot(arg);
    }
    std::unordered_map<std::string,std::size_t> varOffsets;
    varOffsets.reserve(l.variables.size());
    for(const auto& name:l.variables){
        if(!variablePhysical.count(name))
            varOffsets.emplace(name,(++frameSlots)*8);
    }
    const auto regSlot=[&](Reg r){
        const std::size_t off=(r<spillOffsets.size()&&spillOffsets[r]!=0)?
            spillOffsets[r]:(frameSlots+1)*8;
        return "QWORD PTR [rbp-"+std::to_string(off)+"]";
    };
    const auto varOffset=[&](const std::string& name)->std::size_t{
        const auto it=varOffsets.find(name);
        return it!=varOffsets.end()?it->second:(frameSlots+1)*8;
    };
    const auto varSlot=[&](const std::string& name){
        return "QWORD PTR [rbp-"+std::to_string(varOffset(name))+"]";
    };
    const auto varAddress=[&](const std::string& name){
        return "[rbp-"+std::to_string(varOffset(name))+"]";
    };
    std::vector<std::size_t> fixedStorage(fn.code.size(),0);
    std::size_t frameBytes=frameSlots*8;
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
        return it!=variablePhysical.end()?it->second:varSlot(name);
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
    // Copy named locals directly to/from allocated registers where possible.
    // x86-64 MOV cannot copy memory to memory; keep RAX as the reserved
    // scratch register for those cases, preserving existing spill behavior.
    const auto registerOperand=[&](Reg r)->std::string{
        const auto it=physical.find(r);
        return it==physical.end()?regSlot(r):it->second;
    };
    const auto copyWord=[&](const std::string& destination,
                           const std::string& source){
        if(destination==source)return;
        const bool destinationMemory=destination.rfind("QWORD PTR [",0)==0;
        const bool sourceMemory=source.rfind("QWORD PTR [",0)==0;
        if(destinationMemory&&sourceMemory)
            out<<"    mov rax, "<<source<<"\n"
               <<"    mov "<<destination<<", rax\n";
        else
            out<<"    mov "<<destination<<", "<<source<<"\n";
    };
    // Emitting one unused assembler label per NIR opcode forces the assembler
    // and linker to process needless symbols. Only branch destinations need
    // PC labels; diagnostic/trap labels remain emitted at their own sites.
    std::unordered_set<std::size_t> jumpTargets;
    jumpTargets.reserve(fn.code.size()/8+8);
    std::vector<std::size_t> useCounts(fn.nextReg,0);
    for(const auto& i:fn.code){
        if(i.op==NirOp::Jump||i.op==NirOp::JumpIfFalse)
            jumpTargets.insert(i.target);
        for(auto r:i.args)if(r<useCounts.size())++useCounts[r];
    }
    // Pre-resolve fixed-size stack arrays through their one-time named
    // binding: StackAlloc -> Store("array") -> Load("array") -> PtrOffset.
    // The binding must be initialized along the straight-line function
    // entry before any branch and must never be reassigned or address-taken.
    // This proves that rbp-relative addressing is identical to following
    // the materialized pointer, even inside nested sort/matrix loops.
    const std::size_t noStackRoot=fn.code.size();
    std::vector<std::size_t> stackRoot(fn.nextReg,noStackRoot);
    struct FixedBinding {std::size_t stores=0,pc=0;Reg source=0;bool taken=false;};
    std::unordered_map<std::string,FixedBinding> bindings;
    std::vector<bool> prefixControl(fn.code.size()+1,false);
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        prefixControl[pc+1]=prefixControl[pc]||
            in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse||
            in.op==NirOp::Return||in.op==NirOp::Throw||
            in.op==NirOp::Try||in.op==NirOp::InlineAsm;
        if(in.op==NirOp::StackAlloc&&in.dest&&
           *in.dest<stackRoot.size()&&definitions[*in.dest]==1&&
           fixedStorage[pc]!=0)
            stackRoot[*in.dest]=pc;
        if(in.op==NirOp::Store){
            auto& binding=bindings[in.text];
            ++binding.stores;
            binding.pc=pc;
            if(in.args.size()==1)binding.source=in.args[0];
            else binding.stores=2; // malformed stores cannot establish a binding
        }
        if(in.op==NirOp::AddressOf)bindings[in.text].taken=true;
    }
    for(std::size_t pc=0;pc<fn.code.size();++pc){
        const auto& in=fn.code[pc];
        if(in.op!=NirOp::Load||!in.dest||*in.dest>=stackRoot.size()||
           definitions[*in.dest]!=1||in.isVolatile)continue;
        const auto it=bindings.find(in.text);
        if(it==bindings.end())continue;
        const auto& binding=it->second;
        if(binding.stores!=1||binding.taken||
           binding.pc>=fn.code.size()||
           prefixControl[binding.pc]||
           binding.source>=stackRoot.size()||
           stackRoot[binding.source]==noStackRoot)continue;
        // Because no control transfer precedes Store, it dominates every
        // later Load. An earlier Load cannot use its future value.
        if(pc<=binding.pc)continue;
        stackRoot[*in.dest]=stackRoot[binding.source];
    }
    std::unordered_set<std::size_t> fusedBranches;
    std::unordered_set<std::size_t> fusedPointers;
    for(std::size_t pc=0;pc+1<fn.code.size();++pc){
        const auto& pointer=fn.code[pc];
        if(pointer.op!=NirOp::PtrOffset||!pointer.dest||
           pointer.args.size()!=2||
           pointer.width!=8||
           *pointer.dest>=useCounts.size()||
           useCounts[*pointer.dest]!=1)
            continue;
        std::size_t nextPc=pc+1;
        while(nextPc<fn.code.size()&&fn.code[nextPc].op==NirOp::Nop)++nextPc;
        if(nextPc<fn.code.size()&&!jumpTargets.count(nextPc)){
            const auto& access=fn.code[nextPc];
            if((access.op==NirOp::LoadMemory||access.op==NirOp::StoreMemory)&&
               access.width==8&&
               !access.args.empty()&&access.args[0]==*pointer.dest)
                fusedPointers.insert(pc);
        }
    }
    // Fuse a single-use scalar array LoadMemory into its immediately
    // following compare-and-branch. One or two side-effect-free named
    // local/constant definitions may separate the load and comparison
    // (e.g. the insertion-sort key). The address is held in reserved R8
    // until CMP, never across a call, write, volatile access or new CFG
    // entry. Pointer loads and bounds traps still occur in source order.
    std::unordered_map<std::size_t,std::size_t> memoryComparisons;
    std::unordered_set<std::size_t> delayedLoads;
    for(std::size_t pc=1;pc+1<fn.code.size();++pc){
        const auto& compare=fn.code[pc];
        const auto& branch=fn.code[pc+1];
        if(compare.op!=NirOp::Binary||!compare.dest||
           compare.args.size()!=2||compare.isVolatile||
           *compare.dest>=useCounts.size()||
           useCounts[*compare.dest]!=1||
           branch.op!=NirOp::JumpIfFalse||
           branch.args.size()!=1||
           branch.args[0]!=*compare.dest||
           jumpTargets.count(pc)||jumpTargets.count(pc+1))continue;
        if(compare.text!="=="&&compare.text!="!="&&
           compare.text!="<"&&compare.text!="<="&&
           compare.text!=">"&&compare.text!=">=")continue;
        const auto earliest=pc>4?pc-4:0;
        for(std::size_t prior=pc;prior-->earliest;){
            const auto& load=fn.code[prior];
            if(load.op==NirOp::LoadMemory&&load.dest&&
               *load.dest==compare.args[0]&&load.width==8&&
               // Only full-width integer comparisons qualify. Float
               // comparisons have NaN/ordering behavior and an SSE2 path
               // that must never consume a deferred integer load.
               (load.type.kind==TypeKind::Int||
                load.type.kind==TypeKind::I64||
                load.type.kind==TypeKind::U64||
                load.type.kind==TypeKind::Usize||
                load.type.kind==TypeKind::Isize)&&
               compare.args[1]<l.regKinds.size()&&
               l.regKinds[compare.args[1]]!=ValueKind::Float&&
               l.regKinds[compare.args[1]]!=ValueKind::String&&
               !load.isVolatile&&load.args.size()==1&&
               *load.dest<definitions.size()&&definitions[*load.dest]==1&&
               useCounts[*load.dest]==1){
                bool safe=true;
                for(std::size_t middle=prior+1;middle<pc;++middle){
                    const auto& between=fn.code[middle];
                    if(jumpTargets.count(middle)||
                       !((between.op==NirOp::Load&&!between.isVolatile)||
                         (between.op==NirOp::Const)||
                         (between.op==NirOp::Nop))){
                        safe=false;break;
                    }
                }
                if(safe){
                    memoryComparisons.emplace(pc,prior);
                    delayedLoads.insert(prior);
                }
                break;
            }
            if(load.op!=NirOp::Load&&! (load.op==NirOp::Const) &&
               load.op!=NirOp::Nop)break;
        }
    }
    // Experimental two-lane 64-bit SLP: use real SSE2 paddq/psubq for
    // independent scalar operations in one straight-line region. Disabled
    // by default because pack/unpack overhead can exceed scalar cost; no
    // broad vectorization or performance claim is implied by this path.
    // The intermediate instructions may only be nonvolatile scalar
    // definitions, so delaying the first wrapping arithmetic is safe.
    std::unordered_set<std::size_t> packedStarts;
    std::unordered_map<std::size_t,std::size_t> packedEnds;
    const char* simdEnv=std::getenv("NOQERI_EXPERIMENTAL_SIMD");
    const bool experimentalSimd=simdEnv&&std::string(simdEnv)=="1";
    const auto integer64=[](TypeKind kind){
        return kind==TypeKind::Int||kind==TypeKind::I64||
               kind==TypeKind::U64||kind==TypeKind::Usize||
               kind==TypeKind::Isize;
    };
    if(experimentalSimd){
        for(std::size_t pc=0;pc<fn.code.size();++pc){
            const auto& first=fn.code[pc];
            if(first.op!=NirOp::Binary||!first.dest||
               first.args.size()!=2||first.isVolatile||
               (first.text!="+"&&first.text!="-")||
               !integer64(first.type.kind))continue;
            const auto last=std::min(fn.code.size(),pc+7);
            for(std::size_t at=pc+1;at<last;++at){
                const auto& second=fn.code[at];
                if(jumpTargets.count(at))break;
                if(second.op==NirOp::Binary&&second.dest&&
                   second.args.size()==2&&!second.isVolatile&&
                   second.text==first.text&&
                   second.type.kind==first.type.kind&&
                   *first.dest!=*second.dest&&
                   std::find(second.args.begin(),second.args.end(),
                             *first.dest)==second.args.end()){
                    bool compatible=true;
                    for(const auto r:{first.args[0],first.args[1],
                                      second.args[0],second.args[1]}){
                        const auto k=r<fn.registerTypes.size()?
                            fn.registerTypes[r].kind:TypeKind::Unknown;
                        if(k!=TypeKind::Unknown&&!integer64(k))
                            compatible=false;
                        if(r==*first.dest||r==*second.dest)
                            compatible=false;
                    }
                    // Reject any intermediate overwrite of a source of
                    // either lane. Pairing would otherwise observe a value
                    // from the wrong program point (before SSA renaming).
                    for(std::size_t mid=pc+1;mid<at;++mid)
                        if(fn.code[mid].dest){
                            const auto d=*fn.code[mid].dest;
                            if(d==first.args[0]||d==first.args[1]||
                               d==second.args[0]||d==second.args[1]||
                               d==*first.dest||d==*second.dest)
                                compatible=false;
                        }
                    if(compatible){
                        packedStarts.insert(pc);
                        packedEnds.emplace(at,pc);
                        pc=at;
                    }
                    break;
                }
                // Delaying the first instruction is only legal when
                // no intermediate NIR instruction redefines its operands
                // or destinations. This matters for pre-SSA virtual regs.
                if(second.dest&&
                   (*second.dest==first.args[0]||
                    *second.dest==first.args[1]||
                    *second.dest==*first.dest))break;
                const bool pure=second.op==NirOp::Nop||
                    (second.op==NirOp::Load&&!second.isVolatile)||
                    (second.op==NirOp::Const&&
                     std::holds_alternative<std::int64_t>(second.literal))||
                    (second.op==NirOp::Cast&&second.args.size()==1&&
                     integer64(second.type.kind));
                if(!pure||std::find(second.args.begin(),second.args.end(),
                                   *first.dest)!=second.args.end())
                    break;
            }
        }
    }
    // Automatic, conservative two-lane native loop vectorization.
    // Only private contiguous 2 x 64-bit stack objects and fully proven
    // load/load/add-or-sub/add-or-sub/store/store groups are eligible.
    // At least FOUR independent groups must share one invariant RHS, so
    // the hoisted broadcast is amortized in array-heavy loop bodies.
    // No data-dependent indices, escapes, volatile access, alias-unknown
    // pointers, arbitrary reordering or loop-tail rewriting are allowed.
    // NOQERI_NATIVE_LOOP_SIMD=0 disables this feature for local A/B tests.
    std::unordered_set<std::size_t> loopSimdStarts;
    std::unordered_set<std::size_t> loopSimdSkipped;
    std::unordered_map<std::size_t,std::size_t> loopSimdStackRoot;
    std::unordered_map<std::size_t,bool> loopSimdSubtract;
    std::unordered_set<std::size_t> loopSimdBroadcastAt;
    const char* loopEnv=std::getenv("NOQERI_NATIVE_LOOP_SIMD");
    const bool loopSimd=(!loopEnv||std::string(loopEnv)!="0")&&
        !experimentalSimd;
    if(loopSimd){
        // Each source register is classified once. Repeating a full
        // function scan for every candidate pack made compilation quadratic
        // for large kernels with hundreds of contiguous stack objects.
        std::vector<std::uint8_t> nonprivateRoots(fn.nextReg,0);
        std::vector<std::uint8_t> escapingPointers(fn.nextReg,0);
        std::vector<bool> mayClobberAfter(fn.code.size()+1,false);
        for(std::size_t pc=fn.code.size();pc-->0;){
            const auto& in=fn.code[pc];
            mayClobberAfter[pc]=mayClobberAfter[pc+1]||
                in.op==NirOp::Call||in.op==NirOp::Intrinsic||
                in.op==NirOp::InlineAsm;
            for(std::size_t k=0;k<in.args.size();++k){
                const Reg r=in.args[k];
                if(r>=fn.nextReg)continue;
                if(!(k==0&&in.op==NirOp::PtrOffset))
                    nonprivateRoots[r]=1;
                if(!(k==0&&(in.op==NirOp::LoadMemory||
                            in.op==NirOp::StoreMemory)))
                    escapingPointers[r]=1;
            }
        }
        std::unordered_map<Reg,std::size_t> onlyDefinitions;
        for(std::size_t pc=0;pc<fn.code.size();++pc)
            if(fn.code[pc].dest&&
               *fn.code[pc].dest<definitions.size()&&
               definitions[*fn.code[pc].dest]==1)
                onlyDefinitions[*fn.code[pc].dest]=pc;
        const auto pointerInfo=[&](Reg ptr)
            ->std::optional<std::pair<Reg,std::int64_t>>{
            const auto it=onlyDefinitions.find(ptr);
            if(it==onlyDefinitions.end())return std::nullopt;
            const auto& p=fn.code[it->second];
            if(p.op!=NirOp::PtrOffset||p.width!=8||
               p.args.size()!=2)return std::nullopt;
            const auto offset=constantValues.find(p.args[1]);
            if(offset==constantValues.end())return std::nullopt;
            return std::make_pair(p.args[0],offset->second);
        };
        for(std::size_t pc=0;pc+5<fn.code.size();++pc){
            const auto& a=fn.code[pc];
            const auto& b=fn.code[pc+1];
            const auto& addA=fn.code[pc+2];
            const auto& addB=fn.code[pc+3];
            const auto& stA=fn.code[pc+4];
            const auto& stB=fn.code[pc+5];
            if(a.op!=NirOp::LoadMemory||b.op!=NirOp::LoadMemory||
               addA.op!=NirOp::Binary||addB.op!=NirOp::Binary||
               stA.op!=NirOp::StoreMemory||stB.op!=NirOp::StoreMemory||
               !a.dest||!b.dest||!addA.dest||!addB.dest||
               a.isVolatile||b.isVolatile||addA.isVolatile||
               addB.isVolatile||stA.isVolatile||stB.isVolatile||
               a.width!=8||b.width!=8||stA.width!=8||stB.width!=8||
               a.args.size()!=1||b.args.size()!=1||
               addA.args.size()!=2||addB.args.size()!=2||
               stA.args.size()!=2||stB.args.size()!=2||
               (addA.text!="+"&&addA.text!="-")||
               addB.text!=addA.text||
               addA.type.kind!=addB.type.kind||
               !integer64(addA.type.kind)||
               addA.args[0]!=*a.dest||addB.args[0]!=*b.dest||
               addA.args[1]!=addB.args[1]||
               stA.args[0]!=a.args[0]||stB.args[0]!=b.args[0]||
               stA.args[1]!=*addA.dest||stB.args[1]!=*addB.dest||
               *a.dest>=useCounts.size()||
               *b.dest>=useCounts.size()||
               *addA.dest>=useCounts.size()||
               *addB.dest>=useCounts.size()||
               useCounts[*a.dest]!=1||useCounts[*b.dest]!=1||
               useCounts[*addA.dest]!=1||useCounts[*addB.dest]!=1)
                continue;
            if(std::any_of(fn.code.begin()+pc,fn.code.begin()+pc+6,
                [](const NirInstruction& in){return in.isVolatile;}))
                continue;
            bool branchEntry=false;
            for(std::size_t pos=pc;pos<=pc+5;++pos)
                if(jumpTargets.count(pos)){branchEntry=true;break;}
            if(branchEntry)continue;
            const auto ptrA=pointerInfo(a.args[0]);
            const auto ptrB=pointerInfo(b.args[0]);
            if(!ptrA||!ptrB||ptrA->first!=ptrB->first||
               ptrA->second!=0||ptrB->second!=1)continue;
            const auto root=onlyDefinitions.find(ptrA->first);
            if(root==onlyDefinitions.end()||
               fn.code[root->second].op!=NirOp::StackAlloc||
               fn.code[root->second].width<16)continue;
            if(ptrA->first>=nonprivateRoots.size()||
               a.args[0]>=escapingPointers.size()||
               b.args[0]>=escapingPointers.size()||
               nonprivateRoots[ptrA->first]||
               escapingPointers[a.args[0]]||
               escapingPointers[b.args[0]])continue;
            std::size_t loopHeader=fn.code.size();
            for(std::size_t end=pc+6;end<fn.code.size();++end){
                const auto& edge=fn.code[end];
                if(edge.op==NirOp::Jump&&edge.target<pc&&
                   edge.target<end){loopHeader=edge.target;break;}
            }
            if(loopHeader==fn.code.size())continue;
            // Hoist the broadcast into a unique constant definition
            // preceding the whole loop. No opaque calls or SIMD clobbers
            // may occur between setup and the loop backedge.
            const auto rhs=onlyDefinitions.find(addA.args[1]);
            if(rhs==onlyDefinitions.end()||
               rhs->second>=loopHeader)continue;
            const auto& rhsValue=fn.code[rhs->second];
            if(rhsValue.op!=NirOp::Const||
               !std::holds_alternative<std::int64_t>(rhsValue.literal))
                continue;
            // Hoisted XMM5 must be initialized on every path to the
            // loop. A straight-line entry prefix before the constant is
            // a conservative dominance proof; do not speculate across
            // branches, early returns or exception handling.
            bool straightPrefix=true;
            for(std::size_t at=0;at<rhs->second;++at){
                const auto op=fn.code[at].op;
                if(op==NirOp::Jump||op==NirOp::JumpIfFalse||
                   op==NirOp::Return||op==NirOp::Throw||
                   op==NirOp::Try||op==NirOp::InlineAsm){
                    straightPrefix=false;break;
                }
            }
            if(!straightPrefix||jumpTargets.count(rhs->second)||
               mayClobberAfter[rhs->second+1]||
               (!loopSimdBroadcastAt.empty()&&
                !loopSimdBroadcastAt.count(rhs->second)))continue;
            loopSimdStarts.insert(pc);
            loopSimdStackRoot[pc]=root->second;
            loopSimdSubtract[pc]=(addA.text=="-");
            loopSimdBroadcastAt.insert(rhs->second);
            for(std::size_t skip=pc+1;skip<=pc+5;++skip)
                loopSimdSkipped.insert(skip);
            pc+=5;
        }
    // Below this threshold the setup and extra vector bookkeeping may
    // cost more than the handful of scalar instructions they replace.
    if(loopSimdStarts.size()<4){
        loopSimdStarts.clear();
        loopSimdSkipped.clear();
        loopSimdStackRoot.clear();
        loopSimdSubtract.clear();
        loopSimdBroadcastAt.clear();
    }
    }
    // One memory instruction can address a fixed local array directly via
    // RBP + index*stride + displacement. Avoid materializing the aggregate
    // base pointer on every read/shift in insertion sort, and on every
    // strided multiplication in a small matrix kernel. Bounds checks remain
    // separate and fully active; only address calculation is shortened.
    const auto fixedStackAddress=[&](const NirInstruction& pointer)
        ->std::optional<std::string>{
        if(pointer.op!=NirOp::PtrOffset||pointer.args.size()!=2||
           pointer.width!=8)return std::nullopt;
        const Reg base=pointer.args[0],index=pointer.args[1];
        if(base>=stackRoot.size()||stackRoot[base]==noStackRoot||
           index>=fn.nextReg)return std::nullopt;
        const auto root=stackRoot[base];
        if(root>=fixedStorage.size()||fixedStorage[root]==0)return std::nullopt;
        const auto physicallyAllocated=physical.find(index);
        std::string indexReg="r11";
        if(physicallyAllocated!=physical.end())
            indexReg=physicallyAllocated->second;
        else load(index,"r11");
        return "QWORD PTR [rbp - "+std::to_string(fixedStorage[root])+
               " + "+indexReg+"*8]";
    };
    // For arbitrary pointer-backed scalar arrays, x86 can use an allocated
    // base and index as memory operands without two redundant MOVs through
    // RAX/R11. This is safe for any fused PtrOffset of a full-width access,
    // even when no fixed StackAlloc provenance can be established.
    const auto registerIndexedAddress=[&](const NirInstruction& pointer){
        const Reg base=pointer.args[0],index=pointer.args[1];
        const auto basePhysical=physical.find(base);
        const auto indexPhysical=physical.find(index);
        const std::string baseReg=basePhysical==physical.end()?"rax":
            basePhysical->second;
        const std::string indexReg=indexPhysical==physical.end()?"r11":
            indexPhysical->second;
        if(basePhysical==physical.end())load(base,"rax");
        if(indexPhysical==physical.end())load(index,"r11");
        return "QWORD PTR ["+baseReg+" + "+indexReg+"*"+
               std::to_string(pointer.width)+"]";
    };

    // Fold PtrOffset(stackRoot, add_or_sub(idx, small_imm)) into [rbp - adjusted_disp + idx*8]
    // and skip emitting the single-use index adjustment arithmetic.
    struct FoldedStackOffset { std::size_t rootPc=0; Reg idxReg=0; std::int64_t elemOffset=0; };
    std::unordered_map<std::size_t,FoldedStackOffset> foldedPtrOffsets;
    std::unordered_set<std::size_t> skippedOffsetBinary;
    for(const std::size_t ptrPc:fusedPointers){
        if(ptrPc==0||jumpTargets.count(ptrPc))continue;
        const auto& ptrIn=fn.code[ptrPc];
        if(ptrIn.op!=NirOp::PtrOffset||ptrIn.args.size()!=2||ptrIn.width!=8)continue;
        const Reg base=ptrIn.args[0],sumIdx=ptrIn.args[1];
        if(base>=stackRoot.size()||stackRoot[base]==noStackRoot)continue;
        const std::size_t root=stackRoot[base];
        if(root>=fixedStorage.size()||fixedStorage[root]==0)continue;
        std::size_t binPc=ptrPc-1;
        while(binPc>0&&fn.code[binPc].op==NirOp::Nop)--binPc;
        const auto& binIn=fn.code[binPc];
        if(binIn.op!=NirOp::Binary||!binIn.dest||*binIn.dest!=sumIdx||
           binIn.args.size()!=2||binIn.isVolatile||(binIn.text!="+"&&binIn.text!="-")||
           definitions[sumIdx]!=1||useCounts[sumIdx]!=1)continue;
        if(jumpTargets.count(binPc+1))continue;
        std::optional<std::pair<Reg,std::int64_t>> adj;
        if(binIn.text=="+"){
            auto rC=constantValues.find(binIn.args[1]);
            auto lC=constantValues.find(binIn.args[0]);
            if(rC!=constantValues.end()&&rC->second>=-1024&&rC->second<=1024)
                adj=std::make_pair(binIn.args[0],rC->second);
            else if(lC!=constantValues.end()&&lC->second>=-1024&&lC->second<=1024)
                adj=std::make_pair(binIn.args[1],lC->second);
        }else{
            auto rC=constantValues.find(binIn.args[1]);
            if(rC!=constantValues.end()&&rC->second>=-1024&&rC->second<=1024)
                adj=std::make_pair(binIn.args[0],-rC->second);
        }
        if(!adj||!physical.count(adj->first))continue;
        const std::int64_t disp=static_cast<std::int64_t>(fixedStorage[root])-adj->second*8;
        if(disp<=0||disp>100000000LL)continue;
        foldedPtrOffsets[ptrPc]={root,adj->first,adj->second};
        skippedOffsetBinary.insert(binPc);
    }
    const auto foldedStackAddress=[&](std::size_t ptrPc)->std::optional<std::string>{
        const auto it=foldedPtrOffsets.find(ptrPc);
        if(it==foldedPtrOffsets.end())return std::nullopt;
        const auto phys=physical.find(it->second.idxReg);
        if(phys==physical.end())return std::nullopt;
        const std::size_t storage=fixedStorage[it->second.rootPc];
        const std::int64_t disp=static_cast<std::int64_t>(storage)-it->second.elemOffset*8;
        if(disp<=0)return std::nullopt;
        return "QWORD PTR [rbp - "+std::to_string(disp)+" + "+phys->second+"*8]";
    };

    // General AVX2 4-lane counted-loop vectorizer for pure/array integer loops.
    struct Avx2LoopPtr {
        std::size_t rootPc=0;
        std::optional<Reg> invOffsetReg; // nullopt => [rbp - storage + rax*8]
    };
    struct Avx2BodyOp {
        enum Kind { Add, Sub, Mul, And, Or, Xor, ShlImm, ShrImm, ModPow2Imm, ModMagic32, LoadArr, StoreArr, AccAdd } kind=Add;
        int dstYmm=-1;
        int lhsYmm=-1;
        int rhsYmm=-1;
        std::int64_t imm=0;
        std::int64_t modMagic=0;
        int modShift=0;
        int ptrIdx=-1;
        bool mulFast32=false;
        bool mulRhsInvariantLow32=false;
    };
    struct Avx2LoopPlan {
        std::size_t header=0,backPc=0;
        Reg idxReg=0,limitReg=0;
        std::string cmpOp;
        bool isUnsigned=false;
        bool hasAcc=false;
        bool usesIdxVec=false;
        Reg accReg=0;
        std::vector<std::pair<Reg,int>> invBroadcasts;
        std::vector<std::pair<std::int64_t,int>> constBroadcasts;
        std::vector<Avx2LoopPtr> ptrs;
        std::vector<Avx2BodyOp> ops;
    };
    std::unordered_map<std::size_t,Avx2LoopPlan> avx2Loops;
    std::unordered_set<std::size_t> avx2JumpRedirects;
    {
        std::unordered_set<std::size_t> escapingRoots;
        for(const auto& in:fn.code){
            for(std::size_t a=0;a<in.args.size();++a){
                const Reg arg=in.args[a];
                if(arg>=stackRoot.size()||stackRoot[arg]==noStackRoot)continue;
                if(a==0&&(in.op==NirOp::LoadMemory||in.op==NirOp::StoreMemory||
                          in.op==NirOp::CheckNonNull||in.op==NirOp::PtrOffset))continue;
                escapingRoots.insert(stackRoot[arg]);
            }
        }
        const auto resolveNonNegLoop=[&](Reg r,const std::unordered_set<Reg>& inLoopDefs){
            if(r<nonNegative.size()&&nonNegative[r])return true;
            if(!inLoopDefs.count(r)){
                auto c=constantValues.find(r);
                if(c!=constantValues.end()&&c->second>=0)return true;
            }
            return false;
        };
        for(std::size_t backPc=0;backPc<fn.code.size();++backPc){
            const auto& backIn=fn.code[backPc];
            if(backIn.op!=NirOp::Jump||backIn.target>=backPc)continue;
            const std::size_t header=backIn.target;
            if(avx2Loops.count(header))continue;
            // Collect guards & validate structured single-entry loop without side effects
            std::vector<std::size_t> guards;
            bool okCtrl=true;
            for(std::size_t pc=header;pc<backPc;++pc){
                const auto& in=fn.code[pc];
                if(in.op==NirOp::JumpIfFalse){
                    if(in.target<=backPc){okCtrl=false;break;}
                    guards.push_back(pc);
                }else if(in.op==NirOp::Jump||in.op==NirOp::Return||in.op==NirOp::Throw||
                         in.op==NirOp::Try||in.op==NirOp::InlineAsm||in.op==NirOp::Call||
                         in.op==NirOp::CheckBounds||in.op==NirOp::CheckNonNull||
                         in.op==NirOp::Load||in.op==NirOp::Store||
                         in.op==NirOp::AtomicLoad||in.op==NirOp::AtomicStore||
                         in.op==NirOp::AtomicExchange||in.op==NirOp::AtomicCompareExchange||
                         in.op==NirOp::AtomicFence||in.op==NirOp::Intrinsic){
                    okCtrl=false;break;
                }
            }
            if(!okCtrl||(guards.size()!=1&&guards.size()!=2))continue;
            if(guards.size()==1&&fn.code[guards[0]].target!=backPc+1)continue;
            // Forbid external jumps entering inside (header, backPc]
            bool extJump=false;
            for(std::size_t pc=0;pc<fn.code.size();++pc){
                if(pc>=header&&pc<=backPc)continue;
                const auto& in=fn.code[pc];
                if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&
                   in.target>header&&in.target<=backPc){extJump=true;break;}
            }
            if(extJump)continue;

            // Resolve pre-guard aliases and header compare
            const std::size_t guard1Pc=guards[0];
            std::unordered_map<Reg,Reg> preAlias;
            std::size_t cmp1Pc=fn.code.size();
            for(std::size_t pc=header;pc<guard1Pc;++pc){
                const auto& in=fn.code[pc];
                if(in.op==NirOp::Cast&&in.dest&&in.args.size()==1&&
                   integer64(fn.registerTypes[*in.dest].kind)&&
                   integer64(fn.registerTypes[in.args[0]].kind)){
                    Reg src=in.args[0];
                    if(preAlias.count(src))src=preAlias[src];
                    preAlias[*in.dest]=src;
                }else if(in.op==NirOp::Binary&&in.dest&&*in.dest==fn.code[guard1Pc].args[0]){
                    cmp1Pc=pc;
                }
            }
            if(cmp1Pc>=guard1Pc)continue;
            const auto& cmp1=fn.code[cmp1Pc];
            if((cmp1.text!="<"&&cmp1.text!="<=")||cmp1.args.size()!=2)continue;
            Reg idxReg=cmp1.args[0];
            if(preAlias.count(idxReg))idxReg=preAlias[idxReg];
            const Reg limitReg=cmp1.args[1];
            if(!integer64(fn.registerTypes[idxReg].kind)||
               !integer64(fn.registerTypes[limitReg].kind))continue;

            // Collect all definitions inside [header..backPc]
            std::unordered_set<Reg> allLoopDefs;
            for(std::size_t pc=header;pc<backPc;++pc)
                if(fn.code[pc].dest)allLoopDefs.insert(*fn.code[pc].dest);
            if(allLoopDefs.count(limitReg))continue; // limit must be loop-invariant

            // Find induction variable step (+1) and optional reduction acc (+)
            bool isCaseB=(guards.size()==2);
            std::size_t bodyStart=guard1Pc+1;
            std::size_t body1End=backPc;
            Reg accReg=0;bool hasAcc=false;
            std::unordered_set<Reg> skipBodyPcs;

            if(isCaseB){
                // Case B (from unrollGuardedLoop2x): guard1 -> body1 -> idx1=idx+1 -> cmp2(idx1,limit) -> guard2 -> body2 -> idx2=idx1+1 -> latch -> backPc
                for(std::size_t pc=header;pc<backPc;++pc){
                    if(fn.code[pc].op==NirOp::LoadMemory||fn.code[pc].op==NirOp::StoreMemory){
                        okCtrl=false;break;
                    }
                }
                if(!okCtrl)continue;
                const std::size_t guard2Pc=guards[1];
                std::size_t cmp2Pc=fn.code.size();
                for(std::size_t pc=guard1Pc+1;pc<guard2Pc;++pc){
                    const auto& in=fn.code[pc];
                    if(in.op==NirOp::Binary&&in.dest&&*in.dest==fn.code[guard2Pc].args[0])cmp2Pc=pc;
                }
                if(cmp2Pc>=guard2Pc||cmp2Pc==guard1Pc+1)continue;
                const auto& cmp2=fn.code[cmp2Pc];
                if(cmp2.text!=cmp1.text||cmp2.args.size()!=2||cmp2.args[1]!=limitReg)continue;
                const Reg idx1=cmp2.args[0];
                // Expect idx1 = idxReg + 1 right before cmp2Pc
                const std::size_t step1Pc=cmp2Pc-1;
                const auto& st1=fn.code[step1Pc];
                if(st1.op!=NirOp::Binary||st1.text!="+"||!st1.dest||*st1.dest!=idx1||
                   st1.args.size()!=2)continue;
                auto checkStep1=[&](Reg r)->bool{
                    auto resolve=[&](Reg x){return preAlias.count(x)?preAlias[x]:x;};
                    if(resolve(st1.args[0])==r){
                        auto c=constantValues.find(st1.args[1]);
                        return c!=constantValues.end()&&c->second==1;
                    }
                    if(resolve(st1.args[1])==r){
                        auto c=constantValues.find(st1.args[0]);
                        return c!=constantValues.end()&&c->second==1;
                    }
                    return false;
                };
                if(!checkStep1(idxReg))continue;
                skipBodyPcs.insert(step1Pc);
                body1End=step1Pc;
                // Check latch casts right before backPc
                std::size_t cur=backPc;
                std::unordered_map<Reg,Reg> latchMap;
                while(cur>guard2Pc+1&&fn.code[cur-1].op==NirOp::Cast&&fn.code[cur-1].dest&&fn.code[cur-1].args.size()==1){
                    --cur;
                    latchMap[*fn.code[cur].dest]=fn.code[cur].args[0];
                }
                const Reg idx2=latchMap.count(idxReg)?latchMap[idxReg]:idxReg;
                if(cur<=guard2Pc+1)continue;
                const auto& st2=fn.code[cur-1];
                if(st2.op!=NirOp::Binary||st2.text!="+"||!st2.dest||*st2.dest!=idx2||st2.args.size()!=2)continue;
                auto isOne=[&](Reg c){auto it=constantValues.find(c);return it!=constantValues.end()&&it->second==1;};
                if(!((st2.args[0]==idx1&&isOne(st2.args[1]))||(st2.args[1]==idx1&&isOne(st2.args[0]))))continue;
                // Identify acc if present
                for(const auto& [phi,src2]:latchMap){
                    if(phi==idxReg)continue;
                    if(hasAcc){okCtrl=false;break;}
                    hasAcc=true;accReg=phi;
                }
                if(!okCtrl)continue;
                // Verify body2 [guard2Pc+1 .. cur-1) is structurally isomorphic to body1 [bodyStart .. body1End)
                const std::size_t b1Len=body1End-bodyStart;
                const std::size_t b2Start=guard2Pc+1;
                if(cur-1<b2Start||cur-1-b2Start!=b1Len)continue;
                std::unordered_map<Reg,Reg> iso;
                iso[idxReg]=idx1;
                for(const auto& [k,v]:preAlias)if(v==idxReg)iso[k]=idx1;
                if(hasAcc){
                    // acc1 is the reduction result in body1
                    const auto& accAdd1=fn.code[body1End-1];
                    if(accAdd1.op!=NirOp::Binary||accAdd1.text!="+"||!accAdd1.dest||
                       accAdd1.args.size()!=2||
                       (accAdd1.args[0]!=accReg&&accAdd1.args[1]!=accReg)){okCtrl=false;break;}
                    iso[accReg]=*accAdd1.dest;
                }
                for(std::size_t k=0;k<b1Len&&okCtrl;++k){
                    const auto& a=fn.code[bodyStart+k];
                    const auto& b=fn.code[b2Start+k];
                    if(a.op!=b.op||a.text!=b.text||a.args.size()!=b.args.size()||
                       a.dest.has_value()!=b.dest.has_value()){okCtrl=false;break;}
                    for(std::size_t p=0;p<a.args.size();++p){
                        Reg mapped=iso.count(a.args[p])?iso[a.args[p]]:a.args[p];
                        if(mapped!=b.args[p]){okCtrl=false;break;}
                    }
                    if(a.dest&&b.dest)iso[*a.dest]=*b.dest;
                }
                if(!okCtrl)continue;
                if(hasAcc){
                    const auto& accAdd1=fn.code[body1End-1];
                    if(!iso.count(*accAdd1.dest)||iso[*accAdd1.dest]!=latchMap[accReg])continue;
                }
            }else{
                // Case A: single guard loop
                // Parse latch updates at end of [bodyStart .. backPc)
                std::size_t cur=backPc;
                std::unordered_map<Reg,Reg> latchMap;
                while(cur>bodyStart&&fn.code[cur-1].op==NirOp::Cast&&fn.code[cur-1].dest&&fn.code[cur-1].args.size()==1){
                    --cur;
                    latchMap[*fn.code[cur].dest]=fn.code[cur].args[0];
                }
                // Detect direct idx = idx + 1 or idxNext = idx + 1; idx = Cast(idxNext)
                bool foundIdxStep=false;
                for(std::size_t pc=cur;pc>bodyStart;--pc){
                    const auto& in=fn.code[pc-1];
                    if(in.op==NirOp::Binary&&in.text=="+"&&in.dest&&in.args.size()==2){
                        auto resolvesIdx=[&](Reg r){return r==idxReg||(preAlias.count(r)&&preAlias[r]==idxReg);};
                        auto isOne=[&](Reg c){auto it=constantValues.find(c);return it!=constantValues.end()&&it->second==1;};
                        if((resolvesIdx(in.args[0])&&isOne(in.args[1]))||(resolvesIdx(in.args[1])&&isOne(in.args[0]))){
                            if(*in.dest==idxReg||(latchMap.count(idxReg)&&latchMap[idxReg]==*in.dest)){
                                skipBodyPcs.insert(pc-1);
                                foundIdxStep=true;
                                break;
                            }
                        }
                    }
                }
                if(!foundIdxStep)continue;
                for(const auto& [phi,src]:latchMap){
                    if(phi==idxReg)continue;
                    if(hasAcc){okCtrl=false;break;}
                    hasAcc=true;accReg=phi;
                }
                if(!okCtrl)continue;
                body1End=cur;
                if(!hasAcc){
                    // Check for direct self-add acc = acc + term right before idx step
                    for(std::size_t pc=bodyStart;pc<body1End;++pc){
                        if(skipBodyPcs.count(pc))continue;
                        const auto& in=fn.code[pc];
                        if(in.dest&&in.op==NirOp::Binary&&in.text=="+"&&in.args.size()==2&&
                           (*in.dest==in.args[0]||*in.dest==in.args[1])&&
                           *in.dest!=idxReg){
                            // Ensure acc is defined before header (phi) and nowhere else in body
                            if(!hasAcc&&definitions[*in.dest]>=2){
                                hasAcc=true;accReg=*in.dest;
                            }else if(*in.dest==accReg){okCtrl=false;break;}
                        }
                    }
                    if(!okCtrl)continue;
                }
            }
            // Reject if Case A has any uses outside [header..backPc] of values defined inside
            if(!isCaseB){
                for(std::size_t pc=0;pc<fn.code.size();++pc){
                    if(pc>=header&&pc<=backPc)continue;
                    for(Reg a:fn.code[pc].args){
                        if(a==accReg||a==idxReg)continue;
                        if(allLoopDefs.count(a)){okCtrl=false;break;}
                    }
                    if(!okCtrl)break;
                }
                if(!okCtrl)continue;
            }

            // Build vector IR plan for [bodyStart .. body1End)
            Avx2LoopPlan plan;
            plan.header=header;plan.backPc=backPc;
            plan.idxReg=idxReg;plan.limitReg=limitReg;
            plan.cmpOp=cmp1.text;
            plan.isUnsigned=isUnsigned(cmp1.args[0]<fn.registerTypes.size()?
                fn.registerTypes[cmp1.args[0]].kind:TypeKind::Unknown);
            plan.hasAcc=hasAcc;plan.accReg=accReg;

            // Determine address-only regs (used ONLY to construct PtrOffset indices)
            std::unordered_set<Reg> valueUsedRegs;
            for(std::size_t pc=bodyStart;pc<body1End;++pc){
                if(skipBodyPcs.count(pc))continue;
                const auto& in=fn.code[pc];
                if(in.op==NirOp::PtrOffset&&in.dest)continue;
                else if(in.op==NirOp::LoadMemory&&in.args.size()==1)continue;
                else if(in.op==NirOp::StoreMemory&&in.args.size()==2)valueUsedRegs.insert(in.args[1]);
                else for(Reg a:in.args)valueUsedRegs.insert(a);
            }

            // Compute upper bounds ub[reg] inside loop for fast 32-bit multiply/modulo
            std::unordered_map<Reg,std::uint64_t> ub;
            std::unordered_set<Reg> nonNeg;
            // Check idx >= 0 from external defs
            bool idxNonNeg=true;
            for(std::size_t pc=0;pc<fn.code.size();++pc){
                if(pc>=header&&pc<=backPc)continue;
                const auto& in=fn.code[pc];
                if(in.dest&&*in.dest==idxReg){
                    bool ok=false;
                    if(in.op==NirOp::Const){
                        if(auto cv=std::get_if<std::int64_t>(&in.literal);cv&&*cv>=0)ok=true;
                    }else if(in.op==NirOp::Cast&&in.args.size()==1){
                        auto c=constantValues.find(in.args[0]);
                        if((in.args[0]<nonNegative.size()&&nonNegative[in.args[0]])||
                           (c!=constantValues.end()&&c->second>=0))ok=true;
                    }else if(*in.dest<nonNegative.size()&&nonNegative[*in.dest]){
                        ok=true;
                    }
                    if(!ok)idxNonNeg=false;
                }
            }
            for(const auto& [rC,cVal]:constantValues){
                if(!allLoopDefs.count(rC)&&cVal>=0){
                    nonNeg.insert(rC);
                    ub[rC]=static_cast<std::uint64_t>(cVal);
                }
            }
            {
                std::unordered_map<std::string,std::vector<std::size_t>> vStorePcs;
                std::unordered_set<std::string> vAddrTaken;
                for(std::size_t k=0;k<fn.code.size();++k){
                    const auto& ci=fn.code[k];
                    if(ci.op==NirOp::Store)vStorePcs[ci.text].push_back(k);
                    else if(ci.op==NirOp::AddressOf)vAddrTaken.insert(ci.text);
                }
                for(const auto& p:fn.params)vAddrTaken.insert(p);
                std::unordered_map<std::string,std::uint64_t> varUb;
                for(const auto& [vName,spcs]:vStorePcs){
                    if(spcs.empty()||vAddrTaken.count(vName))continue;
                    std::uint64_t bound=0;
                    bool ok=true;
                    for(std::size_t spc:spcs){
                        const auto& st=fn.code[spc];
                        if(st.isVolatile||st.args.size()!=1){ok=false;break;}
                        const Reg val=st.args[0];
                        if(auto cit=constantValues.find(val);cit!=constantValues.end()&&cit->second>=0){
                            bound=std::max(bound,static_cast<std::uint64_t>(cit->second));
                            continue;
                        }
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
                        std::uint64_t gMax=std::numeric_limits<std::uint64_t>::max();
                        for(std::size_t gpc=0;gpc<spc;++gpc){
                            const auto& br=fn.code[gpc];
                            if(br.op!=NirOp::JumpIfFalse||br.args.size()!=1||br.target<=spc)continue;
                            for(std::size_t cpc=0;cpc<gpc;++cpc){
                                const auto& cmp=fn.code[cpc];
                                if(cmp.dest&&*cmp.dest==br.args[0]&&cmp.op==NirOp::Binary&&
                                   (cmp.text=="<"||cmp.text=="<=")&&cmp.args.size()==2){
                                    auto up=constantValues.find(cmp.args[1]);
                                    if(up==constantValues.end()||up->second<=0)continue;
                                    bool matches=false;
                                    for(std::size_t lpc=0;lpc<cpc;++lpc){
                                        if(fn.code[lpc].dest&&*fn.code[lpc].dest==cmp.args[0]&&
                                           fn.code[lpc].op==NirOp::Load&&fn.code[lpc].text==vName){
                                            bool written=false;
                                            for(std::size_t xpc:spcs)if(xpc>lpc&&xpc<spc){written=true;break;}
                                            bool jumpedIn=false;
                                            for(std::size_t jpc=gpc+1;jpc<=spc;++jpc)if(jumpTargets.count(jpc)){jumpedIn=true;break;}
                                            if(!written&&!jumpedIn){matches=true;break;}
                                        }
                                    }
                                    if(matches){
                                        const std::uint64_t incB=static_cast<std::uint64_t>(up->second)+static_cast<std::uint64_t>(step);
                                        gMax=std::min(gMax,incB);
                                    }
                                }
                            }
                        }
                        if(gMax==std::numeric_limits<std::uint64_t>::max()){ok=false;break;}
                        bound=std::max(bound,gMax);
                    }
                    if(ok)varUb[vName]=bound;
                }
                {
                    std::vector<std::vector<std::size_t>> regDefPcs(fn.nextReg);
                    for(std::size_t rpk=0;rpk<fn.code.size();++rpk){
                        if(fn.code[rpk].dest&&*fn.code[rpk].dest<regDefPcs.size())
                            regDefPcs[*fn.code[rpk].dest].push_back(rpk);
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
                    for(std::size_t oBack=0;oBack<fn.code.size();++oBack){
                        const auto& ob=fn.code[oBack];
                        if(ob.op!=NirOp::Jump||ob.target>=oBack)continue;
                        const std::size_t oHdr=ob.target;
                        if(!(oHdr<header&&oBack>=backPc))continue;
                        std::size_t oBr=oHdr;
                        for(std::size_t k=oHdr;k<oBack;++k){
                            if(fn.code[k].op==NirOp::JumpIfFalse&&fn.code[k].target>oBack){oBr=k;break;}
                            if(fn.code[k].op==NirOp::Jump||fn.code[k].op==NirOp::JumpIfFalse)break;
                        }
                        if(oBr<=oHdr||oBr>=header||fn.code[oBr].args.size()!=1)continue;
                        const Reg condR=fn.code[oBr].args[0];
                        for(std::size_t cpc=oHdr;cpc<oBr;++cpc){
                            const auto& cmp=fn.code[cpc];
                            if(!cmp.dest||*cmp.dest!=condR||cmp.op!=NirOp::Binary||
                               (cmp.text!="<"&&cmp.text!="<=")||cmp.args.size()!=2)continue;
                            auto limIt2=constantValues.find(cmp.args[1]);
                            if(limIt2==constantValues.end()||limIt2->second<=0)continue;
                            const Reg idxR=cmp.args[0];
                            if(idxR>=regDefPcs.size()||regDefPcs[idxR].size()!=2)continue;
                            const std::size_t d0=regDefPcs[idxR][0],d1=regDefPcs[idxR][1];
                            if(d0>=oHdr||d1<=oBr||d1>=oBack)continue;
                            bool jumpedIn=false;
                            for(std::size_t src=0;src<fn.code.size();++src){
                                if(src>oBr&&src<=oBack)continue;
                                const auto& jin=fn.code[src];
                                if((jin.op==NirOp::Jump||jin.op==NirOp::JumpIfFalse)&&
                                   jin.target>oBr&&jin.target<=d1){jumpedIn=true;break;}
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
                            if(initVal<0||initVal>limIt2->second)continue;
                            const auto& stepIn=fn.code[d1];
                            if(stepIn.op!=NirOp::Binary||stepIn.text!="+"||stepIn.args.size()!=2)continue;
                            auto stepIt=constantValues.find(stepIn.args[1]);
                            if(stepIt==constantValues.end()||stepIt->second<=0||stepIt->second>1024)continue;
                            if(unwrapCast(stepIn.args[0])!=idxR)continue;
                            std::uint64_t oMax=static_cast<std::uint64_t>(limIt2->second);
                            if(cmp.text=="<"&&oMax>0)--oMax;
                            nonNeg.insert(idxR);
                            ub[idxR]=ub.count(idxR)?std::min(ub[idxR],oMax):oMax;
                        }
                    }
                }
                for(std::size_t k=0;k<header;++k){
                    const auto& ci=fn.code[k];
                    if(!ci.dest||allLoopDefs.count(*ci.dest))continue;
                    const Reg d=*ci.dest;
                    if(ci.op==NirOp::Load&&!ci.isVolatile){
                        if(auto it=varUb.find(ci.text);it!=varUb.end()){
                            nonNeg.insert(d);
                            ub[d]=ub.count(d)?std::min(ub[d],it->second):it->second;
                        }
                    }else if(ci.op==NirOp::Cast&&ci.args.size()==1){
                        if(nonNeg.count(ci.args[0])||resolveNonNegLoop(ci.args[0],allLoopDefs)){
                            nonNeg.insert(d);
                            if(ub.count(ci.args[0]))ub[d]=ub[ci.args[0]];
                        }
                    }else if(ci.op==NirOp::Binary&&ci.args.size()==2){
                        const Reg a=ci.args[0],b=ci.args[1];
                        const bool nna=nonNeg.count(a)||resolveNonNegLoop(a,allLoopDefs);
                        const bool nnb=nonNeg.count(b)||resolveNonNegLoop(b,allLoopDefs);
                        if(nna&&nnb){
                            nonNeg.insert(d);
                            if(ub.count(a)&&ub.count(b)){
                                if(ci.text=="+"&&ub[a]<=UINT64_MAX-ub[b])ub[d]=ub[a]+ub[b];
                                else if(ci.text=="*"&&(ub[a]==0||ub[b]<=UINT64_MAX/ub[a]))ub[d]=ub[a]*ub[b];
                                else if(ci.text=="%"&&ub[b]>0)ub[d]=ub[b]-1;
                                else if(ci.text=="/"&&ub[b]>=1)ub[d]=ub[a];
                                else if(ci.text=="&")ub[d]=std::min(ub[a],ub[b]);
                            }
                        }
                    }
                }
            }
            if(idxNonNeg)nonNeg.insert(idxReg);
            auto limIt=constantValues.find(limitReg);
            if(limIt!=constantValues.end()&&limIt->second>=0&&idxNonNeg){
                std::uint64_t limMax=static_cast<std::uint64_t>(limIt->second);
                if(cmp1.text=="<"&&limMax>0)--limMax;
                ub[idxReg]=limMax;
            }
            for(const auto& [k,v]:preAlias){
                if(nonNeg.count(v))nonNeg.insert(k);
                if(ub.count(v))ub[k]=ub[v];
            }

            // Map regs -> virtual vector slots (-1: unset, -2: idxVec(ymm0), -3: accVec(ymm1), >=0: vreg id)
            std::unordered_map<Reg,int> vmap;
            vmap[idxReg]=-2;
            for(const auto& [k,v]:preAlias)if(v==idxReg)vmap[k]=-2;
            if(hasAcc)vmap[accReg]=-3;

            int nextVreg=0;
            std::unordered_map<std::int64_t,int> constVreg;
            std::unordered_map<Reg,int> invVreg;
            std::unordered_map<Reg,int> ptrIdxMap; // ptrOffset dest -> plan.ptrs index

            const auto getValVreg=[&](Reg r)->int{
                if(vmap.count(r))return vmap[r];
                if(allLoopDefs.count(r))return -1;
                auto c=constantValues.find(r);
                if(c!=constantValues.end()){
                    auto it=constVreg.find(c->second);
                    if(it!=constVreg.end()){vmap[r]=it->second;return it->second;}
                    int vr=nextVreg++;
                    constVreg[c->second]=vr;
                    plan.constBroadcasts.push_back({c->second,vr});
                    vmap[r]=vr;
                    return vr;
                }
                auto it=invVreg.find(r);
                if(it!=invVreg.end()){vmap[r]=it->second;return it->second;}
                int vr=nextVreg++;
                invVreg[r]=vr;
                plan.invBroadcasts.push_back({r,vr});
                vmap[r]=vr;
                return vr;
            };

            // Track live intervals of vregs inside body for register allocation (ymm2..ymm12 = 11 regs)
            struct TempOp { Avx2BodyOp op; int dstVreg=-1; int lhsVreg=-1; int rhsVreg=-1; };
            std::vector<TempOp> bodyOps;
            bool vecOk=true;
            for(std::size_t pc=bodyStart;pc<body1End&&vecOk;++pc){
                if(skipBodyPcs.count(pc))continue;
                const auto& in=fn.code[pc];
                if(in.op==NirOp::Nop)continue;
                if(in.op==NirOp::Const){
                    if(in.dest)vmap[*in.dest]=getValVreg(*in.dest); // unused unless referenced
                    continue;
                }
                if(in.op==NirOp::Cast&&in.dest&&in.args.size()==1){
                    if(!integer64(fn.registerTypes[*in.dest].kind)||
                       !integer64(fn.registerTypes[in.args[0]].kind)){vecOk=false;break;}
                    int srcV=getValVreg(in.args[0]);
                    if(srcV==-1){vecOk=false;break;}
                    vmap[*in.dest]=srcV;
                    if(nonNeg.count(in.args[0]))nonNeg.insert(*in.dest);
                    if(ub.count(in.args[0]))ub[*in.dest]=ub[in.args[0]];
                    continue;
                }
                if(in.op==NirOp::PtrOffset&&in.dest&&in.args.size()==2&&in.width==8){
                    const Reg base=in.args[0],ix=in.args[1];
                    if(base>=stackRoot.size()||stackRoot[base]==noStackRoot||
                       escapingRoots.count(stackRoot[base])){vecOk=false;break;}
                    const std::size_t root=stackRoot[base];
                    if(root>=fixedStorage.size()||fixedStorage[root]==0){vecOk=false;break;}
                    std::optional<Reg> invOff;
                    if(ix==idxReg||(preAlias.count(ix)&&preAlias[ix]==idxReg)){
                        invOff=std::nullopt;
                    }else{
                        // Look for inv + idx in body
                        const NirInstruction* idxBin=nullptr;
                        for(std::size_t k=bodyStart;k<pc;++k){
                            if(fn.code[k].dest&&*fn.code[k].dest==ix){idxBin=&fn.code[k];break;}
                        }
                        if(!idxBin||idxBin->op!=NirOp::Binary||idxBin->text!="+"||idxBin->args.size()!=2){
                            vecOk=false;break;
                        }
                        auto isIdx=[&](Reg x){return x==idxReg||(preAlias.count(x)&&preAlias[x]==idxReg);};
                        if(isIdx(idxBin->args[0])&&!allLoopDefs.count(idxBin->args[1]))invOff=idxBin->args[1];
                        else if(isIdx(idxBin->args[1])&&!allLoopDefs.count(idxBin->args[0]))invOff=idxBin->args[0];
                        else{vecOk=false;break;}
                    }
                    int foundIdx=-1;
                    for(std::size_t pi=0;pi<plan.ptrs.size();++pi){
                        if(plan.ptrs[pi].rootPc==root&&plan.ptrs[pi].invOffsetReg==invOff){
                            foundIdx=static_cast<int>(pi);break;
                        }
                    }
                    if(foundIdx<0){
                        foundIdx=static_cast<int>(plan.ptrs.size());
                        plan.ptrs.push_back({root,invOff});
                    }
                    ptrIdxMap[*in.dest]=foundIdx;
                    continue;
                }
                if(in.op==NirOp::LoadMemory&&in.dest&&in.args.size()==1&&in.width==8&&!in.isVolatile){
                    if(!ptrIdxMap.count(in.args[0])||!integer64(fn.registerTypes[*in.dest].kind)){vecOk=false;break;}
                    int d=nextVreg++;vmap[*in.dest]=d;
                    TempOp top;top.op.kind=Avx2BodyOp::LoadArr;top.op.ptrIdx=ptrIdxMap[in.args[0]];top.dstVreg=d;
                    bodyOps.push_back(top);
                    continue;
                }
                if(in.op==NirOp::StoreMemory&&in.args.size()==2&&in.width==8&&!in.isVolatile){
                    if(!ptrIdxMap.count(in.args[0])){vecOk=false;break;}
                    int valV=getValVreg(in.args[1]);
                    if(valV==-1){vecOk=false;break;}
                    TempOp top;top.op.kind=Avx2BodyOp::StoreArr;top.op.ptrIdx=ptrIdxMap[in.args[0]];top.lhsVreg=valV;
                    bodyOps.push_back(top);
                    continue;
                }
                if(in.op==NirOp::Binary&&in.dest&&in.args.size()==2&&!in.isVolatile){
                    // Skip pure address-index addition consumed ONLY by PtrOffset
                    if(in.text=="+"&&!valueUsedRegs.count(*in.dest)){
                        bool onlyPtr=false;
                        for(std::size_t k=pc+1;k<body1End;++k)
                            if(fn.code[k].op==NirOp::PtrOffset&&fn.code[k].args.size()==2&&fn.code[k].args[1]==*in.dest)
                                onlyPtr=true;
                        if(onlyPtr)continue;
                    }
                    if(!integer64(in.type.kind)){vecOk=false;break;}
                    // Check reduction acc = acc + term
                    if(hasAcc&&in.text=="+"&&(in.args[0]==accReg||in.args[1]==accReg)){
                        Reg term=(in.args[0]==accReg)?in.args[1]:in.args[0];
                        int tv=getValVreg(term);
                        if(tv==-1||tv==-3){vecOk=false;break;}
                        vmap[*in.dest]=-3;
                        TempOp top;top.op.kind=Avx2BodyOp::AccAdd;top.lhsVreg=tv;
                        bodyOps.push_back(top);
                        continue;
                    }
                    if(in.args[0]==accReg||in.args[1]==accReg){vecOk=false;break;}
                    const Reg lhs=in.args[0],rhs=in.args[1];
                    const bool lhsNN=resolveNonNegLoop(lhs,allLoopDefs)||nonNeg.count(lhs);
                    const bool rhsNN=resolveNonNegLoop(rhs,allLoopDefs)||nonNeg.count(rhs);
                    if(in.text=="+"||in.text=="-"||in.text=="&"||in.text=="|"||in.text=="^"){
                        int lv=getValVreg(lhs),rv=getValVreg(rhs);
                        if(lv==-1||rv==-1){vecOk=false;break;}
                        int dv=nextVreg++;vmap[*in.dest]=dv;
                        TempOp top;
                        if(in.text=="+")top.op.kind=Avx2BodyOp::Add;
                        else if(in.text=="-")top.op.kind=Avx2BodyOp::Sub;
                        else if(in.text=="&")top.op.kind=Avx2BodyOp::And;
                        else if(in.text=="|")top.op.kind=Avx2BodyOp::Or;
                        else top.op.kind=Avx2BodyOp::Xor;
                        top.dstVreg=dv;top.lhsVreg=lv;top.rhsVreg=rv;
                        bodyOps.push_back(top);
                        if(in.text=="+"&&lhsNN&&rhsNN){
                            nonNeg.insert(*in.dest);
                            if(ub.count(lhs)&&ub.count(rhs)&&ub[lhs]<=UINT64_MAX-ub[rhs])
                                ub[*in.dest]=ub[lhs]+ub[rhs];
                        }else if(in.text=="&"){
                            if(auto rc=constantValues.find(rhs);rc!=constantValues.end()&&rc->second>=0){
                                nonNeg.insert(*in.dest);
                                ub[*in.dest]=static_cast<std::uint64_t>(rc->second);
                            }
                        }
                    }else if(in.text=="*"){
                        auto rc=constantValues.find(rhs);
                        auto lc=constantValues.find(lhs);
                        std::optional<std::pair<Reg,std::int64_t>> pow2;
                        if(rc!=constantValues.end()&&rc->second>0&&(rc->second&(rc->second-1))==0)
                            pow2=std::make_pair(lhs,rc->second);
                        else if(lc!=constantValues.end()&&lc->second>0&&(lc->second&(lc->second-1))==0)
                            pow2=std::make_pair(rhs,lc->second);
                        if(pow2){
                            int shift=0;std::uint64_t v=static_cast<std::uint64_t>(pow2->second);
                            while((v>>=1)!=0)++shift;
                            int lv=getValVreg(pow2->first);
                            if(lv==-1){vecOk=false;break;}
                            int dv=nextVreg++;vmap[*in.dest]=dv;
                            TempOp top;top.op.kind=Avx2BodyOp::ShlImm;top.op.imm=shift;
                            top.dstVreg=dv;top.lhsVreg=lv;
                            bodyOps.push_back(top);
                            if((pow2->first==lhs?lhsNN:rhsNN)&&ub.count(pow2->first)&&ub[pow2->first]<=(UINT64_MAX>>shift)){
                                nonNeg.insert(*in.dest);
                                ub[*in.dest]=ub[pow2->first]<<shift;
                            }
                        }else{
                            int lv=getValVreg(lhs),rv=getValVreg(rhs);
                            if(lv==-1||rv==-1){vecOk=false;break;}
                            int dv=nextVreg++;vmap[*in.dest]=dv;
                            TempOp top;top.op.kind=Avx2BodyOp::Mul;
                            top.dstVreg=dv;top.lhsVreg=lv;top.rhsVreg=rv;
                            if(lhsNN&&rhsNN&&ub.count(lhs)&&ub.count(rhs)&&
                               ub[lhs]<=0xFFFFFFFFULL&&ub[rhs]<=0xFFFFFFFFULL){
                                top.op.mulFast32=true;
                                nonNeg.insert(*in.dest);
                                ub[*in.dest]=ub[lhs]*ub[rhs];
                            }else if(rhsNN&&!allLoopDefs.count(rhs)&&rc!=constantValues.end()&&
                                     rc->second>=0&&rc->second<=0xFFFFFFFFLL){
                                top.op.mulRhsInvariantLow32=true;
                            }else if(lhsNN&&!allLoopDefs.count(lhs)&&lc!=constantValues.end()&&
                                     lc->second>=0&&lc->second<=0xFFFFFFFFLL){
                                std::swap(top.lhsVreg,top.rhsVreg);
                                top.op.mulRhsInvariantLow32=true;
                            }
                            bodyOps.push_back(top);
                        }
                    }else if(in.text=="<<"||in.text==">>"){
                        auto rc=constantValues.find(rhs);
                        if(rc==constantValues.end()||rc->second<0||rc->second>=64||
                           (in.text==">>"&&!plan.isUnsigned&&!lhsNN)){vecOk=false;break;}
                        int lv=getValVreg(lhs);
                        if(lv==-1){vecOk=false;break;}
                        int dv=nextVreg++;vmap[*in.dest]=dv;
                        TempOp top;
                        top.op.kind=(in.text=="<<")?Avx2BodyOp::ShlImm:Avx2BodyOp::ShrImm;
                        top.op.imm=rc->second;top.dstVreg=dv;top.lhsVreg=lv;
                        bodyOps.push_back(top);
                    }else if(in.text=="%"){
                        auto rc=constantValues.find(rhs);
                        if(rc==constantValues.end()||rc->second<=0||!lhsNN){vecOk=false;break;}
                        const std::uint64_t d=static_cast<std::uint64_t>(rc->second);
                        int lv=getValVreg(lhs);
                        if(lv==-1){vecOk=false;break;}
                        if((d&(d-1))==0){
                            // % 2^k => vpand (d - 1)
                            auto mcIt=constVreg.find(static_cast<std::int64_t>(d-1));
                            int maskV=-1;
                            if(mcIt!=constVreg.end())maskV=mcIt->second;
                            else{
                                maskV=nextVreg++;
                                constVreg[static_cast<std::int64_t>(d-1)]=maskV;
                                plan.constBroadcasts.push_back({static_cast<std::int64_t>(d-1),maskV});
                            }
                            int dv=nextVreg++;vmap[*in.dest]=dv;
                            TempOp top;top.op.kind=Avx2BodyOp::ModPow2Imm;
                            top.dstVreg=dv;top.lhsVreg=lv;top.rhsVreg=maskV;
                            bodyOps.push_back(top);
                            nonNeg.insert(*in.dest);
                            ub[*in.dest]=d-1;
                        }else if(ub.count(lhs)&&ub[lhs]<=0xFFFFFFFFULL&&d<=0x7FFFFFFFULL){
                            // 32-bit exact magic multiply: (x * M) >> shift
                            const std::uint64_t nMax=ub[lhs];
                            std::uint64_t chosenM=0;
                            int chosenShift=-1;
                            for(int sh=42;sh>=32;--sh){
                                const std::uint64_t pow2s=1ULL<<sh;
                                const std::uint64_t M=(pow2s+d-1)/d;
                                if(M>0xFFFFFFFFULL)continue;
                                const std::uint64_t err=M*d-pow2s;
                                if(err==0||nMax<=(pow2s-1)/err){
                                    chosenM=M;chosenShift=sh;break;
                                }
                            }
                            if(chosenShift<0){vecOk=false;break;}
                            auto getC=[&](std::int64_t cv){
                                auto it=constVreg.find(cv);
                                if(it!=constVreg.end())return it->second;
                                int vr=nextVreg++;constVreg[cv]=vr;
                                plan.constBroadcasts.push_back({cv,vr});
                                return vr;
                            };
                            const int mVreg=getC(static_cast<std::int64_t>(chosenM));
                            const int dVreg=getC(static_cast<std::int64_t>(d));
                            int dv=nextVreg++;vmap[*in.dest]=dv;
                            TempOp top;top.op.kind=Avx2BodyOp::ModMagic32;
                            top.op.modMagic=static_cast<std::int64_t>(chosenM);
                            top.op.modShift=chosenShift;
                            top.dstVreg=dv;top.lhsVreg=lv;top.rhsVreg=mVreg;
                            // encode divisor vreg in imm
                            top.op.imm=dVreg;
                            bodyOps.push_back(top);
                            nonNeg.insert(*in.dest);
                            ub[*in.dest]=d-1;
                        }else{vecOk=false;break;}
                    }else{vecOk=false;break;}
                    continue;
                }
                vecOk=false;break;
            }
            if(!vecOk||bodyOps.empty())continue;

            // Check aliasing of dynamic pointers & count dynamic base pointers <= 2
            std::size_t dynPtrCount=0;
            for(const auto& p:plan.ptrs)if(p.invOffsetReg.has_value())++dynPtrCount;
            if(dynPtrCount>3)continue;
            bool aliasOk=true;
            for(const auto& opA:bodyOps){
                if(opA.op.kind!=Avx2BodyOp::StoreArr)continue;
                const auto& ptrS=plan.ptrs[opA.op.ptrIdx];
                for(const auto& opB:bodyOps){
                    if(opB.op.kind!=Avx2BodyOp::LoadArr&&opB.op.kind!=Avx2BodyOp::StoreArr)continue;
                    const auto& ptrX=plan.ptrs[opB.op.ptrIdx];
                    if(ptrX.rootPc==ptrS.rootPc&&ptrX.invOffsetReg!=ptrS.invOffsetReg){
                        aliasOk=false;break;
                    }
                }
                if(!aliasOk)break;
            }
            if(!aliasOk)continue;

            // Allocate ymm2..ymm12 (11 regs) to vregs
            std::vector<int> vLastUse(nextVreg,-1);
            for(const auto& b:plan.invBroadcasts)vLastUse[b.second]=static_cast<int>(bodyOps.size());
            for(const auto& c:plan.constBroadcasts)vLastUse[c.second]=static_cast<int>(bodyOps.size());
            for(int iOp=0;iOp<static_cast<int>(bodyOps.size());++iOp){
                const auto& top=bodyOps[iOp];
                if(top.lhsVreg>=0)vLastUse[top.lhsVreg]=std::max(vLastUse[top.lhsVreg],iOp);
                if(top.rhsVreg>=0)vLastUse[top.rhsVreg]=std::max(vLastUse[top.rhsVreg],iOp);
            }
            std::vector<int> vregToYmm(nextVreg,-1);
            bool ymmBusy[13]={};
            for(int y=0;y<=1;++y)ymmBusy[y]=true; // ymm0=idx, ymm1=acc
            const auto allocYmm=[&]()->int{
                for(int y=2;y<=12;++y){if(!ymmBusy[y]){ymmBusy[y]=true;return y;}}
                return -1;
            };
            bool regOk=true;
            for(auto& b:plan.invBroadcasts){
                int y=allocYmm();if(y<0){regOk=false;break;}
                vregToYmm[b.second]=y;b.second=y;
            }
            for(auto& c:plan.constBroadcasts){
                int y=allocYmm();if(y<0){regOk=false;break;}
                vregToYmm[c.second]=y;c.second=y;
            }
            if(!regOk)continue;
            const auto resolveYmm=[&](int vr)->int{
                if(vr==-2)return 0;
                if(vr==-3)return 1;
                return vr>=0?vregToYmm[vr]:-1;
            };
            for(int iOp=0;iOp<static_cast<int>(bodyOps.size());++iOp){
                auto& top=bodyOps[iOp];
                if(top.lhsVreg>=0&&vLastUse[top.lhsVreg]==iOp){
                    int y=vregToYmm[top.lhsVreg];
                    if(y>=2)ymmBusy[y]=false;
                }
                if(top.rhsVreg>=0&&vLastUse[top.rhsVreg]==iOp&&top.rhsVreg!=top.lhsVreg){
                    int y=vregToYmm[top.rhsVreg];
                    if(y>=2)ymmBusy[y]=false;
                }
                if(top.dstVreg>=0){
                    int y=allocYmm();if(y<0){regOk=false;break;}
                    vregToYmm[top.dstVreg]=y;
                }
                Avx2BodyOp finalOp=top.op;
                finalOp.dstYmm=resolveYmm(top.dstVreg);
                finalOp.lhsYmm=resolveYmm(top.lhsVreg);
                finalOp.rhsYmm=resolveYmm(top.rhsVreg);
                if(finalOp.kind==Avx2BodyOp::ModMagic32)
                    finalOp.imm=resolveYmm(static_cast<int>(finalOp.imm));
                if(finalOp.lhsYmm==0||finalOp.rhsYmm==0)plan.usesIdxVec=true;
                plan.ops.push_back(finalOp);
            }
            if(!regOk)continue;
            avx2Loops.emplace(header,std::move(plan));
            avx2JumpRedirects.insert(backPc);
        }
    }

    std::vector<std::string> coldTrapLabels;
    std::vector<std::string> coldBoundsTrapLabels;
    coldTrapLabels.reserve(fn.code.size()/8+1);
    bool hasLegacySseInFn=!loopSimdStarts.empty()||!packedEnds.empty();
    if(!hasLegacySseInFn){
        for(const auto& ci:fn.code){
            if((ci.op==NirOp::Unary||ci.op==NirOp::Binary||ci.op==NirOp::Cast)&&
               (ci.type.kind==TypeKind::Float||
                (!ci.args.empty()&&ci.args[0]<fn.registerTypes.size()&&
                 fn.registerTypes[ci.args[0]].kind==TypeKind::Float))){
                hasLegacySseInFn=true;break;
            }
        }
    }
    const bool keepPerLoopVzeroupper=hasLegacySseInFn;
    for(std::size_t pc=0;pc<fn.code.size();++pc){const auto&i=fn.code[pc];
        if(jumpTargets.count(pc))out<<pcLabel(fn,pc)<<":\n";
        if(skippedOffsetBinary.count(pc))continue;
        if(delayedLoads.count(pc)){
            // Materialize the address, not its value, for a later memory
            // comparison in the SAME straight-line basic block.
            if(pc>0&&fusedPointers.count(pc-1)){
                const auto& pointer=fn.code[pc-1];
                const auto fixed=fixedStackAddress(pointer);
                const auto address=fixed?*fixed:registerIndexedAddress(pointer);
                out<<"    lea r8, "<<address.substr(sizeof("QWORD PTR ")-1)<<"\n";
            }else load(i.args[0],"r8");
            continue;
        }
        if(loopSimdSkipped.count(pc))continue;
        if(loopSimdStarts.count(pc)){
            const auto root=loopSimdStackRoot.at(pc);
            const auto storage=fixedStorage[root];
            // This allocation and both pointer offsets were statically
            // proved private and contiguous. XMM5 holds one loop-invariant
            // scalar broadcast (and is untouched by the guarded region).
            out<<"    movdqu xmm0, XMMWORD PTR [rbp-"<<storage<<"]\n"
               <<(loopSimdSubtract.at(pc)?
                   "    psubq xmm0, xmm5\n":"    paddq xmm0, xmm5\n")
               <<"    movdqu XMMWORD PTR [rbp-"<<storage<<"], xmm0\n";
            continue;
        }
        if(fusedBranches.count(pc)||packedStarts.count(pc))continue;
        const auto pair=packedEnds.find(pc);
        if(pair!=packedEnds.end()){
            const auto& first=fn.code[pair->second];
            // Pack two independent 64-bit lanes, apply one SSE2 vector
            // instruction, then extract both destinations. XMM0..XMM3 are
            // volatile registers in the supported x86-64 calling ABIs.
            load(first.args[0],"rax");
            out<<"    movq xmm0, rax\n";
            load(i.args[0],"rax");
            out<<"    movq xmm1, rax\n"
               <<"    punpcklqdq xmm0, xmm1\n";
            if(first.args[1]==i.args[1]){
                // Broadcast one invariant RHS to both lanes. Avoid one
                // operand load, one XMM move and a redundant unpack.
                load(first.args[1],"rax");
                out<<"    movq xmm2, rax\n"
                   <<"    punpcklqdq xmm2, xmm2\n";
            }else{
                load(first.args[1],"rax");
                out<<"    movq xmm2, rax\n";
                load(i.args[1],"rax");
                out<<"    movq xmm3, rax\n"
                   <<"    punpcklqdq xmm2, xmm3\n";
            }
            out<<(i.text=="+"?"    paddq xmm0, xmm2\n":
                              "    psubq xmm0, xmm2\n")
               <<"    movq rax, xmm0\n";
            store(*first.dest,"rax");
            out<<"    psrldq xmm0, 8\n"
               <<"    movq rax, xmm0\n";
            store(*i.dest,"rax");
            continue;
        }
        if(const auto avxIt=avx2Loops.find(pc);avxIt!=avx2Loops.end()){
            const auto& plan=avxIt->second;
            const std::string scalarLbl=pcLabel(fn,pc)+"_scalar";
            const std::string okLbl=localLabel(fn,pc,"avx2_ok");
            const std::string loopLbl=localLabel(fn,pc,"avx2_loop");
            out<<"    cmp BYTE PTR [rip + .L_noqeri_avx2_state], 1\n"
               <<"    je "<<okLbl<<"\n"
               <<"    ja "<<scalarLbl<<"\n"
               <<"    call noqeri_rt_has_avx2\n"
               <<"    cmp eax, 1\n"
               <<"    jne "<<scalarLbl<<"\n"
               <<okLbl<<":\n";
            load(plan.idxReg,"rax");
            if(auto lc=constantValues.find(plan.limitReg);lc!=constantValues.end()){
                if(lc->second==0)out<<"    xor r11d, r11d\n";
                else out<<"    mov r11, "<<lc->second<<"\n";
            }else{
                load(plan.limitReg,"r11");
            }
            out<<"    cmp rax, r11\n";
            if(plan.cmpOp=="<")
                out<<(plan.isUnsigned?"    jae ":"    jge ")<<scalarLbl<<"\n";
            else
                out<<(plan.isUnsigned?"    ja ":"    jg ")<<scalarLbl<<"\n";
            out<<"    sub r11, rax\n";
            if(plan.cmpOp=="<"){
                out<<"    cmp r11, 4\n"
                   <<"    jb "<<scalarLbl<<"\n";
            }else{
                out<<"    cmp r11, 3\n"
                   <<"    jb "<<scalarLbl<<"\n"
                   <<"    add r11, 1\n"
                   <<"    jc "<<scalarLbl<<"\n";
            }
            out<<"    and r11, -4\n"
               <<"    add r11, rax\n";
            for(const auto& [rInv,y]:plan.invBroadcasts){
                if(auto cv=constantValues.find(rInv);cv!=constantValues.end()){
                    if(cv->second==0){out<<"    vpxor xmm"<<y<<", xmm"<<y<<", xmm"<<y<<"\n";continue;}
                    out<<"    mov rax, "<<cv->second<<"\n";
                }else{
                    load(rInv,"rax");
                }
                out<<"    vmovq xmm"<<y<<", rax\n"
                   <<"    vpbroadcastq ymm"<<y<<", xmm"<<y<<"\n";
            }
            for(const auto& [cVal,y]:plan.constBroadcasts){
                if(cVal==0){
                    out<<"    vpxor xmm"<<y<<", xmm"<<y<<", xmm"<<y<<"\n";
                }else{
                    out<<"    mov rax, "<<cVal<<"\n"
                       <<"    vmovq xmm"<<y<<", rax\n"
                       <<"    vpbroadcastq ymm"<<y<<", xmm"<<y<<"\n";
                }
            }
            std::vector<std::size_t> ptrStorage(plan.ptrs.size(),0);
            std::vector<int> ptrDynReg(plan.ptrs.size(),-1);
            std::size_t minFixedStorage=std::numeric_limits<std::size_t>::max();
            static const char* kDynBaseRegs[3]={"rcx","r10","r8"};
            int dynSlot=0;
            for(std::size_t pi=0;pi<plan.ptrs.size();++pi){
                const auto& p=plan.ptrs[pi];
                const auto storage=fixedStorage[p.rootPc];
                ptrStorage[pi]=storage;
                if(!p.invOffsetReg){
                    if(storage<minFixedStorage)minFixedStorage=storage;
                }else{
                    ptrDynReg[pi]=dynSlot++;
                    const char* bReg=kDynBaseRegs[ptrDynReg[pi]];
                    load(*p.invOffsetReg,"rax");
                    out<<"    lea "<<bReg<<", [rbp - "<<storage<<" + rax*8]\n";
                }
            }
            const auto ptrMemAt=[&](int pi,std::size_t elemDisp)->std::string{
                const std::size_t bytes=elemDisp*8;
                if(ptrDynReg[pi]<0){
                    return "YMMWORD PTR [rbp-"+std::to_string(ptrStorage[pi]-bytes)+"+rax*8]";
                }
                const char* bReg=kDynBaseRegs[ptrDynReg[pi]];
                if(bytes==0)return std::string("YMMWORD PTR [")+bReg+"+rax*8]";
                return std::string("YMMWORD PTR [")+bReg+"+rax*8+"+std::to_string(bytes)+"]";
            };
            load(plan.idxReg,"rax");
            if(plan.usesIdxVec){
                out<<"    vmovq xmm0, rax\n"
                   <<"    vpbroadcastq ymm0, xmm0\n"
                   <<"    vpaddq ymm0, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_iota4]\n";
            }
            if(plan.hasAcc)out<<"    vpxor xmm1, xmm1, xmm1\n";
            int hoistedMulHiSrcYmm=-1;
            {
                std::unordered_set<int> invYmms;
                for(const auto& b:plan.invBroadcasts)invYmms.insert(b.second);
                for(const auto& op:plan.ops){
                    if(op.kind==Avx2BodyOp::Mul&&!op.mulFast32&&!op.mulRhsInvariantLow32){
                        if(invYmms.count(op.lhsYmm)){
                            hoistedMulHiSrcYmm=op.lhsYmm;
                            out<<"    vpsrlq ymm13, ymm"<<hoistedMulHiSrcYmm<<", 32\n";
                            break;
                        }
                    }
                }
            }
            const auto emitBodySlice=[&](std::size_t elemDisp,int accYmm=1,int idxYmm=0){
                const auto mapReg=[&](int y){return (plan.usesIdxVec&&idxYmm!=0&&y==0)?idxYmm:y;};
                for(const auto& op:plan.ops){
                    const int d=op.dstYmm,l=mapReg(op.lhsYmm),r=mapReg(op.rhsYmm);
                    switch(op.kind){
                        case Avx2BodyOp::Add: out<<"    vpaddq ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::Sub: out<<"    vpsubq ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::And: out<<"    vpand ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::Or:  out<<"    vpor ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::Xor: out<<"    vpxor ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::ShlImm: out<<"    vpsllq ymm"<<d<<", ymm"<<l<<", "<<op.imm<<"\n"; break;
                        case Avx2BodyOp::ShrImm: out<<"    vpsrlq ymm"<<d<<", ymm"<<l<<", "<<op.imm<<"\n"; break;
                        case Avx2BodyOp::ModPow2Imm: out<<"    vpand ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"; break;
                        case Avx2BodyOp::ModMagic32:{
                            const int dYmm=static_cast<int>(op.imm);
                            out<<"    vpmuludq ymm15, ymm"<<l<<", ymm"<<r<<"\n"
                               <<"    vpsrlq ymm15, ymm15, "<<op.modShift<<"\n"
                               <<"    vpmuludq ymm14, ymm15, ymm"<<dYmm<<"\n"
                               <<"    vpsubq ymm"<<d<<", ymm"<<l<<", ymm14\n";
                            break;
                        }
                        case Avx2BodyOp::Mul:{
                            if(op.mulFast32){
                                out<<"    vpmuludq ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n";
                            }else if(op.mulRhsInvariantLow32){
                                out<<"    vpsrlq ymm14, ymm"<<l<<", 32\n"
                                   <<"    vpmuludq ymm14, ymm14, ymm"<<r<<"\n"
                                   <<"    vpsllq ymm14, ymm14, 32\n"
                                   <<"    vpmuludq ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"
                                   <<"    vpaddq ymm"<<d<<", ymm"<<d<<", ymm14\n";
                            }else{
                                if(l==hoistedMulHiSrcYmm){
                                    out<<"    vpmuludq ymm14, ymm13, ymm"<<r<<"\n";
                                }else{
                                    out<<"    vpsrlq ymm14, ymm"<<l<<", 32\n"
                                       <<"    vpmuludq ymm14, ymm14, ymm"<<r<<"\n";
                                }
                                out<<"    vpsrlq ymm15, ymm"<<r<<", 32\n"
                                   <<"    vpmuludq ymm15, ymm"<<l<<", ymm15\n"
                                   <<"    vpaddq ymm14, ymm14, ymm15\n"
                                   <<"    vpsllq ymm14, ymm14, 32\n"
                                   <<"    vpmuludq ymm"<<d<<", ymm"<<l<<", ymm"<<r<<"\n"
                                   <<"    vpaddq ymm"<<d<<", ymm"<<d<<", ymm14\n";
                            }
                            break;
                        }
                        case Avx2BodyOp::LoadArr: out<<"    vmovdqu ymm"<<d<<", "<<ptrMemAt(op.ptrIdx,elemDisp)<<"\n"; break;
                        case Avx2BodyOp::StoreArr: out<<"    vmovdqu "<<ptrMemAt(op.ptrIdx,elemDisp)<<", ymm"<<l<<"\n"; break;
                        case Avx2BodyOp::AccAdd: out<<"    vpaddq ymm"<<accYmm<<", ymm"<<accYmm<<", ymm"<<l<<"\n"; break;
                    }
                }
            };
            const bool canUnroll4=!plan.isUnsigned&&!plan.usesIdxVec&&
                plan.ops.size()<=8&&minFixedStorage>96;
            const bool canUnroll2=!plan.isUnsigned&&!canUnroll4&&
                plan.ops.size()<=24&&minFixedStorage>32;
            const bool use4Acc=canUnroll4&&plan.hasAcc&&std::none_of(
                plan.ops.begin(),plan.ops.end(),
                [](const Avx2BodyOp& op){return op.kind==Avx2BodyOp::Mul||op.kind==Avx2BodyOp::ModMagic32;});
            const bool useDualIdx=canUnroll2&&plan.usesIdxVec&&hoistedMulHiSrcYmm<0&&!plan.hasAcc;
            const std::string tail4Lbl=localLabel(fn,pc,"avx2_tail4");
            const std::string doneVecLbl=localLabel(fn,pc,"avx2_done");
            if(canUnroll4){
                const std::string loop16Lbl=localLabel(fn,pc,"avx2_loop16");
                if(use4Acc)
                    out<<"    vpxor xmm13, xmm13, xmm13\n"
                       <<"    vpxor xmm14, xmm14, xmm14\n"
                       <<"    vpxor xmm15, xmm15, xmm15\n";
                out<<"    sub r11, 12\n"
                   <<"    cmp rax, r11\n"
                   <<"    jge "<<tail4Lbl<<"\n"
                   <<loop16Lbl<<":\n";
                emitBodySlice(0,1);
                emitBodySlice(4,use4Acc?13:1);
                emitBodySlice(8,use4Acc?14:1);
                emitBodySlice(12,use4Acc?15:1);
                out<<"    add rax, 16\n"
                   <<"    cmp rax, r11\n"
                   <<"    jl "<<loop16Lbl<<"\n"
                   <<tail4Lbl<<":\n"
                   <<"    add r11, 12\n"
                   <<"    cmp rax, r11\n"
                   <<"    jge "<<doneVecLbl<<"\n";
            }else if(canUnroll2){
                const std::string loop8Lbl=localLabel(fn,pc,"avx2_loop8");
                if(useDualIdx)
                    out<<"    vpaddq ymm13, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_step4]\n";
                out<<"    sub r11, 4\n"
                   <<"    cmp rax, r11\n"
                   <<"    jge "<<tail4Lbl<<"\n"
                   <<loop8Lbl<<":\n";
                if(useDualIdx){
                    emitBodySlice(0,1,0);
                    emitBodySlice(4,1,13);
                    out<<"    vpaddq ymm0, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_step8]\n"
                       <<"    vpaddq ymm13, ymm13, YMMWORD PTR [rip + .L_noqeri_vec_step8]\n"
                       <<"    add rax, 8\n";
                }else{
                    emitBodySlice(0);
                    if(plan.usesIdxVec)
                        out<<"    vpaddq ymm0, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_step4]\n";
                    out<<"    add rax, 4\n";
                    emitBodySlice(0);
                    if(plan.usesIdxVec)
                        out<<"    vpaddq ymm0, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_step4]\n";
                    out<<"    add rax, 4\n";
                }
                out<<"    cmp rax, r11\n"
                   <<"    jl "<<loop8Lbl<<"\n"
                   <<tail4Lbl<<":\n"
                   <<"    add r11, 4\n"
                   <<"    cmp rax, r11\n"
                   <<"    jge "<<doneVecLbl<<"\n";
            }
            out<<loopLbl<<":\n";
            emitBodySlice(0);
            if(plan.usesIdxVec)
                out<<"    vpaddq ymm0, ymm0, YMMWORD PTR [rip + .L_noqeri_vec_step4]\n";
            out<<"    add rax, 4\n"
               <<"    cmp rax, r11\n"
               <<"    jne "<<loopLbl<<"\n"
               <<doneVecLbl<<":\n";
            store(plan.idxReg,"rax");
            if(plan.hasAcc){
                if(use4Acc)
                    out<<"    vpaddq ymm1, ymm1, ymm13\n"
                       <<"    vpaddq ymm14, ymm14, ymm15\n"
                       <<"    vpaddq ymm1, ymm1, ymm14\n";
                out<<"    vextracti128 xmm14, ymm1, 1\n"
                   <<"    vpaddq xmm1, xmm1, xmm14\n"
                   <<"    vpsrldq xmm14, xmm1, 8\n"
                   <<"    vpaddq xmm1, xmm1, xmm14\n"
                   <<"    vmovq rax, xmm1\n";
                load(plan.accReg,"r11");
                out<<"    add rax, r11\n";
                store(plan.accReg,"rax");
            }
            if(keepPerLoopVzeroupper)out<<"    vzeroupper\n";
            out<<scalarLbl<<":\n";
        }
        switch(i.op){
        case NirOp::Const:{
            if(!i.dest)break;
            const auto target = registerOperand(*i.dest);
            const bool isReg = physical.count(*i.dest);
            if(auto v=std::get_if<std::int64_t>(&i.literal)){
                if(*v==0 && isReg){
                    out<<"    xor "<<target<<", "<<target<<"\n";
                }else if(isReg){
                    out<<"    mov "<<target<<", "<<*v<<"\n";
                }else if(*v>=std::numeric_limits<std::int32_t>::min()&&*v<=std::numeric_limits<std::int32_t>::max()){
                    out<<"    mov "<<target<<", "<<*v<<"\n";
                }else{
                    out<<"    mov rax, "<<*v<<"\n";
                    store(*i.dest,"rax");
                }
            }else if(auto v=std::get_if<bool>(&i.literal)){
                if(!*v && isReg){
                    out<<"    xor "<<target<<", "<<target<<"\n";
                }else{
                    out<<"    mov "<<target<<", "<<(*v?1:0)<<"\n";
                }
            }else if(auto v=std::get_if<std::string>(&i.literal)){
                auto s=strings.find(*v);
                if(s==strings.end()){diagnostics.error("NQR-N5005",{},"internal string-pool error");return false;}
                out<<"    lea rax, [rip + "<<s->second<<"]\n";
                store(*i.dest,"rax");
            }else if(auto v=std::get_if<double>(&i.literal)){
                std::uint64_t bits=0;
                static_assert(sizeof(bits)==sizeof(*v),"native f64 requires 64-bit IEEE storage");
                std::memcpy(&bits,v,sizeof(bits));
                out<<"    mov rax, 0x"<<std::hex<<bits<<std::dec<<"\n";
                store(*i.dest,"rax");
            }else if(std::holds_alternative<std::monostate>(i.literal)){
                if(isReg) out<<"    xor "<<target<<", "<<target<<"\n";
                else { out<<"    xor eax, eax\n"; store(*i.dest,"rax"); }
            }else{
                diagnostics.error("NQR-N5001",{},"unsupported native literal");return false;
            }
            if(loopSimdBroadcastAt.count(pc)){
                load(*i.dest,"rax");
                out<<"    movq xmm5, rax\n"
                   <<"    punpcklqdq xmm5, xmm5\n";
            }
            break;}
        case NirOp::Load:if(i.dest){
            copyWord(registerOperand(*i.dest),variableOperand(i.text));
        }break;
        case NirOp::Store:if(!i.args.empty()){
            copyWord(variableOperand(i.text),registerOperand(i.args[0]));
        }break;
        case NirOp::AddressOf:if(i.dest){out<<"    lea rax, "<<varAddress(i.text)<<"\n";store(*i.dest,"rax");}break;
        case NirOp::LoadMemory:if(i.dest&&!i.args.empty()){
            const bool sign=*i.dest<fn.registerTypes.size()&&
                signedNarrow(fn.registerTypes[*i.dest].kind);
            // For full-width scalar reads, load into the allocated register
            // itself. Avoid the RAX -> r12-r15 hop in array-heavy loops.
            const auto destination=physical.find(*i.dest);
            const bool directQword=i.width==8&&destination!=physical.end();
            std::optional<std::string> stackAddr;
            if(i.width==8){
                std::size_t priorPc=pc;
                while(priorPc>0&&fn.code[priorPc-1].op==NirOp::Nop)--priorPc;
                if(priorPc>0&&fusedPointers.count(priorPc-1)){
                    stackAddr=foldedStackAddress(priorPc-1);
                    if(!stackAddr)stackAddr=fixedStackAddress(fn.code[priorPc-1]);
                    if(!stackAddr)stackAddr=registerIndexedAddress(fn.code[priorPc-1]);
                }
            }
            if(stackAddr){
                if(directQword)
                    out<<"    mov "<<destination->second<<", "<<*stackAddr<<"\n";
                else out<<"    mov rax, "<<*stackAddr<<"\n";
            }else{
                const auto basePhys=physical.find(i.args[0]);
                const std::string baseReg=basePhys!=physical.end()?basePhys->second:"rax";
                if(basePhys==physical.end())load(i.args[0],"rax");
                if(directQword)
                    out<<"    mov "<<destination->second<<", QWORD PTR ["<<baseReg<<"]\n";
                else{
                    if(baseReg!="rax")out<<"    mov rax, "<<baseReg<<"\n";
                    emitLoadWidth(out,i.width,sign);
                }
            }
            if(!directQword)store(*i.dest,"rax");
        }break;
        case NirOp::StoreMemory:if(i.args.size()==2){
            // Similarly, when a full-width store already has its value in
            // an allocated register, skip staging it through R10.
            const auto value=physical.find(i.args[1]);
            const bool directQword=i.width==8&&value!=physical.end();
            std::optional<std::string> stackAddr;
            if(i.width==8){
                std::size_t priorPc=pc;
                while(priorPc>0&&fn.code[priorPc-1].op==NirOp::Nop)--priorPc;
                if(priorPc>0&&fusedPointers.count(priorPc-1)){
                    stackAddr=foldedStackAddress(priorPc-1);
                    if(!stackAddr)stackAddr=fixedStackAddress(fn.code[priorPc-1]);
                    if(!stackAddr)stackAddr=registerIndexedAddress(fn.code[priorPc-1]);
                }
            }
            if(stackAddr){
                if(directQword)
                    out<<"    mov "<<*stackAddr<<", "<<value->second<<"\n";
                else{
                    load(i.args[1],"r10");
                    out<<"    mov "<<*stackAddr<<", r10\n";
                }
            }else{
                const auto basePhys=physical.find(i.args[0]);
                const std::string baseReg=basePhys!=physical.end()?basePhys->second:"rax";
                if(basePhys==physical.end())load(i.args[0],"rax");
                if(directQword)
                    out<<"    mov QWORD PTR ["<<baseReg<<"], "<<value->second<<"\n";
                else{
                    load(i.args[1],"r10");
                    if(baseReg!="rax")out<<"    mov rax, "<<baseReg<<"\n";
                    emitStoreWidth(out,i.width);
                }
            }
        }break;
        case NirOp::PtrOffset:
            if(fusedPointers.count(pc))break;
            else if(i.dest&&!i.args.empty()){
                const Reg base=i.args[0];
                const auto destPhys=physical.find(*i.dest);
                const std::string destReg=destPhys!=physical.end()?destPhys->second:"rax";
                if(base<stackRoot.size()&&stackRoot[base]!=noStackRoot&&
                   stackRoot[base]<fixedStorage.size()&&fixedStorage[stackRoot[base]]!=0&&
                   i.args.size()>1&&(i.width==1||i.width==2||i.width==4||i.width==8)){
                    const auto indexPhys=physical.find(i.args[1]);
                    const std::string indexReg=indexPhys!=physical.end()?indexPhys->second:"r10";
                    if(indexPhys==physical.end())load(i.args[1],"r10");
                    out<<"    lea "<<destReg<<", [rbp - "<<fixedStorage[stackRoot[base]]
                       <<" + "<<indexReg<<"*"<<i.width<<"]\n";
                    if(destPhys==physical.end())store(*i.dest,"rax");
                    break;
                }
                const auto basePhys=physical.find(base);
                const std::string baseReg=basePhys!=physical.end()?basePhys->second:"rax";
                if(basePhys==physical.end())load(base,"rax");
                if(i.args.size()>1){
                    const auto indexPhys=physical.find(i.args[1]);
                    const std::string indexReg=indexPhys!=physical.end()?indexPhys->second:"r10";
                    if(indexPhys==physical.end())load(i.args[1],"r10");
                    if(i.width==1||i.width==2||i.width==4||i.width==8)
                        out<<"    lea "<<destReg<<", ["<<baseReg<<" + "<<indexReg<<"*"<<i.width<<"]\n";
                    else{
                        out<<"    imul r10, "<<indexReg<<", "<<i.width<<"\n";
                        out<<"    lea "<<destReg<<", ["<<baseReg<<" + r10]\n";
                    }
                }else if(i.target){
                    out<<"    lea "<<destReg<<", ["<<baseReg<<" + "<<i.target<<"]\n";
                }else if(destReg!=baseReg){
                    out<<"    mov "<<destReg<<", "<<baseReg<<"\n";
                }
                if(destPhys==physical.end())store(*i.dest,"rax");
            }break;
        case NirOp::StackAlloc:if(i.dest){if(pc>=fixedStorage.size()||fixedStorage[pc]==0){diagnostics.error("NQR-N5024",{},"internal native stack-allocation layout error");return false;}out<<"    lea rax, [rbp-"<<fixedStorage[pc]<<"]\n";store(*i.dest,"rax");}break;
        case NirOp::MakeSlice:if(i.dest&&i.args.size()==2){if(pc>=fixedStorage.size()||fixedStorage[pc]<16){diagnostics.error("NQR-N5025",{},"internal native slice layout error");return false;}out<<"    lea r11, [rbp-"<<fixedStorage[pc]<<"]\n";load(i.args[0],"rax");out<<"    mov QWORD PTR [r11], rax\n";load(i.args[1],"rax");out<<"    mov QWORD PTR [r11+8], rax\n    mov rax, r11\n";store(*i.dest,"rax");}break;
        case NirOp::SliceData:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");out<<"    mov rax, QWORD PTR [rax]\n";store(*i.dest,"rax");}break;
        case NirOp::SliceLen:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");out<<"    mov rax, QWORD PTR [rax+8]\n";store(*i.dest,"rax");}break;
        case NirOp::CheckNonNull:{
            if(i.args.size()!=1){diagnostics.error("NQR-N5022",{},"malformed NIR null-check instruction");return false;}
            load(i.args[0],"rax");
            const auto failed=localLabel(fn,pc,"nonnull_fail");
            // Normal accesses fall through. Cold trap instructions live
            // outside the loop instead of requiring a taken success branch.
            out<<"    test rax, rax\n    jz "<<failed<<"\n";
            coldTrapLabels.push_back(failed);
            break;
        }
        case NirOp::CheckBounds:{
            if(i.args.size()!=2){diagnostics.error("NQR-N5023",{},"malformed NIR bounds-check instruction");return false;}
            const auto failed=localLabel(fn,pc,"bounds_fail");
            coldBoundsTrapLabels.push_back(failed);
            const auto fixed=constantValues.find(i.args[1]);
            // For fixed nonnegative lengths, one unsigned comparison is
            // sufficient: every negative i64 index is encoded above the
            // representable length. Compare the allocated register (or
            // stack operand) DIRECTLY instead of copying it into RAX.
            // The memory operand is valid for CMP with both imm32 and a
            // register; x86-64 never permits memory-to-memory CMP.
            if(fixed!=constantValues.end()&&fixed->second>=0){
                const auto index=registerOperand(i.args[0]);
                if(fixed->second<=std::numeric_limits<std::int32_t>::max())
                    out<<"    cmp "<<index<<", "<<fixed->second<<"\n";
                else{
                    out<<"    mov r10, "<<fixed->second<<"\n";
                    out<<"    cmp "<<index<<", r10\n";
                }
                out<<"    jae "<<failed<<"\n";
            }else{
                const auto indexKind=i.args[0]<fn.registerTypes.size()?
                    fn.registerTypes[i.args[0]].kind:TypeKind::Unknown;
                if(isUnsigned(indexKind)){
                    // An unsigned index cannot be negative. Comparing it
                    // directly against the dynamic length avoids a TEST/JS
                    // and two scratch-register moves in tight slice loops.
                    // x86 CMP permits at most one memory operand.
                    const auto index=registerOperand(i.args[0]);
                    const auto length=registerOperand(i.args[1]);
                    const bool indexMemory=index.rfind("QWORD PTR [",0)==0;
                    const bool lengthMemory=length.rfind("QWORD PTR [",0)==0;
                    if(indexMemory&&lengthMemory){
                        load(i.args[0],"rax");
                        out<<"    cmp rax, "<<length<<"\n";
                    }else out<<"    cmp "<<index<<", "<<length<<"\n";
                    out<<"    jae "<<failed<<"\n";
                }else{
                    // Signed and untyped indexes retain the explicit
                    // negative-index trap even for huge dynamic lengths.
                    load(i.args[0],"rax");
                    out<<"    test rax, rax\n    js "<<failed<<"\n";
                    load(i.args[1],"r10");
                    out<<"    cmp rax, r10\n    jae "<<failed<<"\n";
                }
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
        case NirOp::Cast:if(i.dest&&!i.args.empty()){
            const auto sourceKind=i.args[0]<fn.registerTypes.size()
                ?fn.registerTypes[i.args[0]].kind:TypeKind::Unknown;
            const auto targetKind=i.type.kind;
            // SSA Phi destruction uses same-type scalar Cast instructions as
            // edge copies. Do not round-trip each copy through RAX when an
            // allocated physical register or direct stack MOV can do it.
            // Only full-width scalar identity casts qualify: narrow integer
            // casts still need truncation/sign extension.
            const bool identity64=sourceKind==targetKind&&
                (integer64(targetKind)||targetKind==TypeKind::Float||
                 targetKind==TypeKind::Bool);
            if(identity64){
                copyWord(registerOperand(*i.dest),registerOperand(i.args[0]));
                break;
            }
            load(i.args[0],"rax");
            Type sourceType;sourceType.kind=sourceKind;
            Type targetType;targetType.kind=targetKind;
            if(targetKind==TypeKind::Float&&sourceType.isInteger()){
                // NIR registers hold integer values or IEEE-754 binary64 bits.
                // CVTSI2SD is signed; large unsigned u64 values need the
                // round-to-nearest (n >> 1 | n & 1) then double algorithm.
                if(isUnsigned(sourceKind)&&sourceType.size()==8){
                    out<<"    mov r10, rax\n    shr r10, 1\n    and eax, 1\n"
                       <<"    or r10, rax\n    cvtsi2sd xmm0, r10\n    addsd xmm0, xmm0\n";
                }else{
                    if(isUnsigned(sourceKind)&&sourceType.size()==4)
                        out<<"    mov eax, eax\n";
                    out<<"    cvtsi2sd xmm0, rax\n";
                }
                out<<"    movq rax, xmm0\n";
            }else if(sourceKind==TypeKind::Float&&targetType.isInteger()){
                // Float-to-integer casts truncate toward zero. Never allow
                // CVTTSD2SI's indefinite overflow/NaN sentinel to silently
                // become a valid integer value.
                const auto width=static_cast<unsigned>(targetType.size()*8);
                const bool unsignedTarget=isUnsigned(targetKind);
                const double lower=unsignedTarget?-1.0:
                    width==64?-std::ldexp(1.0,63):
                        -std::ldexp(1.0,static_cast<int>(width)-1)-1.0;
                const double upper=std::ldexp(1.0,
                    static_cast<int>(unsignedTarget?width:width-1));
                auto floatingBits=[](double value){
                    std::uint64_t bits=0;
                    std::memcpy(&bits,&value,sizeof(bits));
                    return bits;
                };
                const auto failed=localLabel(fn,pc,"cast_invalid");
                const auto completed=localLabel(fn,pc,"cast_complete");
                out<<"    movq xmm0, rax\n"
                   <<"    mov r10, 0x"<<std::hex<<floatingBits(lower)<<std::dec<<"\n"
                   <<"    movq xmm1, r10\n"
                   <<"    ucomisd xmm0, xmm1\n"
                   <<"    jp "<<failed<<"\n"
                   <<(width==64&&!unsignedTarget?"    jb ":"    jbe ")<<failed<<"\n"
                   <<"    mov r10, 0x"<<std::hex<<floatingBits(upper)<<std::dec<<"\n"
                   <<"    movq xmm1, r10\n"
                   <<"    ucomisd xmm0, xmm1\n"
                   <<"    jp "<<failed<<"\n    jae "<<failed<<"\n";
                if(unsignedTarget&&width==64){
                    const auto small=localLabel(fn,pc,"cast_u64_small");
                    const auto converted=localLabel(fn,pc,"cast_u64_converted");
                    out<<"    mov r10, 0x43e0000000000000\n"
                       <<"    movq xmm1, r10\n    ucomisd xmm0, xmm1\n"
                       <<"    jb "<<small<<"\n"
                       <<"    subsd xmm0, xmm1\n    cvttsd2si rax, xmm0\n"
                       <<"    mov r10, 0x8000000000000000\n"
                       <<"    or rax, r10\n    jmp "<<converted<<"\n"
                       <<small<<":\n    cvttsd2si rax, xmm0\n"
                       <<converted<<":\n";
                }else out<<"    cvttsd2si rax, xmm0\n";
                emitCast(out,i.text);
                out<<"    jmp "<<completed<<"\n"
                   <<failed<<":\n    ud2\n"
                   <<completed<<":\n";
            }else if(targetKind==TypeKind::Float&&sourceKind==TypeKind::Float){
                // Bit-preserving float identity cast.
            }else if(sourceKind==TypeKind::Float){
                diagnostics.error("NQR-N5030",{},"unsupported native float cast target '"+i.text+"'");
                return false;
            }else emitCast(out,i.text);
            store(*i.dest,"rax");
        }break;
        case NirOp::Unary:if(i.dest&&!i.args.empty()){load(i.args[0],"rax");if(i.text=="-"){if(i.type.kind==TypeKind::Float){
            out<<"    mov r10, 0x8000000000000000\n    xor rax, r10\n";
        }else{out<<"    neg rax\n";normalizeIntegerResult(out,i.type.kind);}}else if(i.text=="!")out<<"    test rax, rax\n    sete al\n    movzx rax, al\n";store(*i.dest,"rax");}break;
        case NirOp::Binary:{if(!i.dest||i.args.size()!=2)break;auto a=i.args[0]<l.regKinds.size()?l.regKinds[i.args[0]]:ValueKind::Unknown,b=i.args[1]<l.regKinds.size()?l.regKinds[i.args[1]]:ValueKind::Unknown;if(a==ValueKind::Float||b==ValueKind::Float){
            // Spill registers carry raw IEEE-754 bits. SSE2 is baseline for
            // the qualified x86-64 native-AOT target. No implicit float->int
            // reinterpretation or call-ABI changes are introduced.
            if(a!=ValueKind::Float||b!=ValueKind::Float){
                diagnostics.error("NQR-N5001",{},"mixed float operands must be explicitly cast");
                return false;
            }
            load(i.args[0],"rax");
            load(i.args[1],"r10");
            out<<"    movq xmm0, rax\n    movq xmm1, r10\n";
            if(i.text=="+"||i.text=="-"||i.text=="*"||i.text=="/"){
                if(i.text=="+")out<<"    addsd xmm0, xmm1\n";
                else if(i.text=="-")out<<"    subsd xmm0, xmm1\n";
                else if(i.text=="*")out<<"    mulsd xmm0, xmm1\n";
                else out<<"    divsd xmm0, xmm1\n";
                out<<"    movq rax, xmm0\n";
            }else if(i.text=="=="||i.text=="!="||i.text=="<"||
                     i.text=="<="||i.text==">"||i.text==">="){
                // UCOMISD sets PF for NaN. Ordered operations must reject
                // unordered comparisons; != is true for NaN.
                out<<"    ucomisd xmm0, xmm1\n";
                if(i.text=="==")out<<"    sete al\n    setnp dl\n    and al, dl\n";
                else if(i.text=="!=")out<<"    setne al\n    setp dl\n    or al, dl\n";
                else if(i.text=="<")out<<"    setb al\n    setnp dl\n    and al, dl\n";
                else if(i.text=="<=")out<<"    setbe al\n    setnp dl\n    and al, dl\n";
                else if(i.text==">")out<<"    seta al\n";
                else out<<"    setae al\n";
                out<<"    movzx rax, al\n";
            }else{
                diagnostics.error("NQR-N5001",{},"unsupported native floating-point operator");
                return false;
            }
            store(*i.dest,"rax");
            break;
        }if(a==ValueKind::String||b==ValueKind::String){diagnostics.error("NQR-N5002",{},"runtime string binary operations are not implemented yet");return false;}const auto operandKind=i.args[0]<fn.registerTypes.size()?
            fn.registerTypes[i.args[0]].kind:TypeKind::Unknown;
        const bool unsign=isComparison(i.text) ?
            (isUnsigned(operandKind)||
             (i.args[1]<fn.registerTypes.size()&&
              isUnsigned(fn.registerTypes[i.args[1]].kind))) :
            (isUnsigned(operandKind)||
             (i.args[0]<nonNegative.size()&&nonNegative[i.args[0]]));
        // If a comparison feeds only the immediately following conditional
        // branch, emit one compare and a direct inverted jcc. Preserve NIR
        // short-circuit structure: RHS evaluation is never speculated.
        const bool singleUse=i.dest&&*i.dest<useCounts.size()&&useCounts[*i.dest]==1;
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
            const auto delayed=memoryComparisons.find(pc);
            if(delayed!=memoryComparisons.end()){
                const auto rhsPhysical=physical.find(i.args[1]);
                if(rhsPhysical!=physical.end())
                    out<<"    cmp QWORD PTR [r8], "<<rhsPhysical->second<<"\n";
                else{
                    load(i.args[1],"r10");
                    out<<"    cmp QWORD PTR [r8], r10\n";
                }
                out<<"    "<<inverted<<" "
                   <<pcLabel(fn,fn.code[pc+1].target)<<"\n";
                fusedBranches.insert(pc+1);
                break;
            }
            const auto lhsOp = registerOperand(i.args[0]);
            const bool lhsMem = lhsOp.rfind("QWORD PTR [", 0) == 0;
            const auto rhsImmediate=constantValues.find(i.args[1]);
            if(rhsImmediate!=constantValues.end()&&
               rhsImmediate->second>=std::numeric_limits<std::int32_t>::min()&&
               rhsImmediate->second<=std::numeric_limits<std::int32_t>::max()){
                out<<"    cmp "<<lhsOp<<", "<<rhsImmediate->second<<"\n";
            }else{
                const auto rhsOp = registerOperand(i.args[1]);
                const bool rhsMem = rhsOp.rfind("QWORD PTR [", 0) == 0;
                if(lhsMem && rhsMem){
                    load(i.args[0],"rax");
                    out<<"    cmp rax, "<<rhsOp<<"\n";
                }else{
                    out<<"    cmp "<<lhsOp<<", "<<rhsOp<<"\n";
                }
            }
            out
               <<"    "<<inverted<<" "<<pcLabel(fn,fn.code[pc+1].target)<<"\n";
            fusedBranches.insert(pc+1);
            break;
        }
        if(i.dest){
            const auto destPhysical=physical.find(*i.dest);
            const bool destIsReg=destPhysical!=physical.end();
            const bool is64BitAlu=(i.text=="+"||i.text=="-"||i.text=="*")&&
                (i.type.kind==TypeKind::Int||i.type.kind==TypeKind::I64||
                 i.type.kind==TypeKind::U64||i.type.kind==TypeKind::Usize||
                 i.type.kind==TypeKind::Isize);
            if(destIsReg&&is64BitAlu){
                const std::string targetReg=destPhysical->second;
                const auto rhsImmediate=constantValues.find(i.args[1]);
                const bool hasImmediate=rhsImmediate!=constantValues.end()&&
                    rhsImmediate->second>=std::numeric_limits<std::int32_t>::min()&&
                    rhsImmediate->second<=std::numeric_limits<std::int32_t>::max();
                const std::string srcReg=registerOperand(i.args[0]);
                if(hasImmediate){
                    if(i.text=="*"){
                        const auto factor=rhsImmediate->second;
                        const auto bits=static_cast<std::uint64_t>(factor);
                        if(factor==0){
                            out<<"    xor "<<targetReg<<", "<<targetReg<<"\n";
                        }else if(factor>0&&!(bits&(bits-1))){
                            unsigned shift=0;
                            for(auto x=bits;x>1;x>>=1)++shift;
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            if(shift)out<<"    shl "<<targetReg<<", "<<shift<<"\n";
                        }else{
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            out<<"    imul "<<targetReg<<", "<<targetReg<<", "<<factor<<"\n";
                        }
                    }else if(i.text=="+"){
                        const auto imm=rhsImmediate->second;
                        if(imm==0){
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                        }else{
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            out<<"    add "<<targetReg<<", "<<imm<<"\n";
                        }
                    }else{ // "-"
                        const auto imm=rhsImmediate->second;
                        if(imm==0){
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                        }else{
                            if(srcReg!=targetReg)
                                out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            out<<"    sub "<<targetReg<<", "<<imm<<"\n";
                        }
                    }
                }else{
                    const auto rhs=registerOperand(i.args[1]);
                    if(i.text=="+"){
                        if(srcReg==targetReg)out<<"    add "<<targetReg<<", "<<rhs<<"\n";
                        else if(rhs==targetReg)out<<"    add "<<targetReg<<", "<<srcReg<<"\n";
                        else{
                            out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            out<<"    add "<<targetReg<<", "<<rhs<<"\n";
                        }
                    }else if(i.text=="*"){
                        if(targetReg==rhs)out<<"    imul "<<targetReg<<", "<<srcReg<<"\n";
                        else{
                            if(srcReg!=targetReg)out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                            out<<"    imul "<<targetReg<<", "<<rhs<<"\n";
                        }
                    }else{ // "-"
                        if(srcReg!=targetReg)out<<"    mov "<<targetReg<<", "<<srcReg<<"\n";
                        out<<"    sub "<<targetReg<<", "<<rhs<<"\n";
                    }
                }
                break;
            }
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
                emitUnsignedConstantDivision(out,magnitude,remainder);
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
            // x86-64 sign-extends imm32 for integer operations. Using the
            // literal directly removes an operand load and a live scratch
            // register in common counted loops and arithmetic hot paths.
            // Only single-definition constants are eligible (see above).
            const auto rhsImmediate=constantValues.find(i.args[1]);
            const bool hasImmediate=rhsImmediate!=constantValues.end()&&
                rhsImmediate->second>=std::numeric_limits<std::int32_t>::min()&&
                rhsImmediate->second<=std::numeric_limits<std::int32_t>::max()&&
                (i.text=="+"||i.text=="-"||i.text=="*");
            if(hasImmediate) {
                if(i.text=="*"){
                    const auto factor=rhsImmediate->second;
                    const auto bits=static_cast<std::uint64_t>(factor);
                    // Multiplication by a positive power of two is the
                    // same modular integer operation as SHL. Matrix row
                    // offsets such as row*16 become one inexpensive shift.
                    // Narrow integer result normalization still follows.
                    if(factor>0&&!(bits&(bits-1))){
                        unsigned shift=0;
                        for(auto x=bits;x>1;x>>=1)++shift;
                        if(shift)out<<"    shl rax, "<<shift<<"\n";
                    }else if(factor==0)out<<"    xor eax, eax\n";
                    else out<<"    imul rax, rax, "<<factor<<"\n";
                }else out<<"    "<<(i.text=="+"?"add":"sub")
                        <<" rax, "<<rhsImmediate->second<<"\n";
            }else{
                // Integer ALU operations accept r/m64 operands directly.
                // Avoid copying hot register or spilled RHS values into
                // R10 for ordinary add/sub/mul/compare instructions.
                // Logical operators retain the normalized-R10 path.
                const auto rhs=registerOperand(i.args[1]);
                if(i.text=="+")out<<"    add rax, "<<rhs<<"\n";
                else if(i.text=="-")out<<"    sub rax, "<<rhs<<"\n";
                else if(i.text=="*")out<<"    imul rax, "<<rhs<<"\n";
                else if(i.text=="&&"||i.text=="||"){
                    load(i.args[1],"r10");
                    out<<"    test rax, rax\n    setne al\n"
                       <<"    movzx rax, al\n    test r10, r10\n"
                       <<"    setne r10b\n    movzx r10, r10b\n"
                       <<(i.text=="&&"?"    and rax, r10\n":
                                        "    or rax, r10\n");
                }else{
                    out<<"    cmp rax, "<<rhs<<"\n";
                    if(i.text=="==")out<<"    sete al\n";
                    else if(i.text=="!=")out<<"    setne al\n";
                    else if(i.text=="<")
                        out<<(unsign?"    setb al\n":"    setl al\n");
                    else if(i.text=="<=")
                        out<<(unsign?"    setbe al\n":"    setle al\n");
                    else if(i.text==">")
                        out<<(unsign?"    seta al\n":"    setg al\n");
                    else if(i.text==">=")
                        out<<(unsign?"    setae al\n":"    setge al\n");
                    else{
                        diagnostics.error("NQR-N5003",{},
                            "unsupported native binary operator '"+i.text+"'");
                        return false;
                    }
                    out<<"    movzx rax, al\n";
                }
            }
            }
        if(i.text=="+"||i.text=="-"||i.text=="*"||
           i.text=="/"||i.text=="%")
            normalizeIntegerResult(out,i.type.kind);
        store(*i.dest,"rax");break;}
        case NirOp::Call:{if(i.text=="abi"||i.text=="host"){diagnostics.error("NQR-N5010",{},"generic named ABI dispatch is not yet lowered by native codegen");return false;}if(!avx2Loops.empty()&&!keepPerLoopVzeroupper)out<<"    vzeroupper\n";if(i.text=="textLength"){if(i.args.size()!=1){diagnostics.error("NQR-N5007",{},"native textLength accepts exactly one string");return false;}load(i.args[0],"rdi");out<<"    call noqeri_rt_text_length\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="print"){if(i.args.size()!=1){diagnostics.error("NQR-N5006",{},"native print accepts exactly one argument");return false;}Reg r=i.args[0];load(r,"rdi");auto k=r<l.regKinds.size()?l.regKinds[r]:ValueKind::Unknown;if(k==ValueKind::String)out<<"    call noqeri_rt_print_cstr\n";else if(k==ValueKind::Bool)out<<"    call noqeri_rt_print_bool\n";else if(k==ValueKind::Float)out<<"    call noqeri_rt_print_float\n";else out<<"    call noqeri_rt_print_int\n";if(i.dest){out<<"    xor eax, eax\n";store(*i.dest,"rax");}}else if(i.text=="clockMillis"){out<<"    call noqeri_rt_clock_millis\n";if(i.dest)store(*i.dest,"rax");}else if(i.text=="platform"){out<<"    call noqeri_rt_platform\n";if(i.dest)store(*i.dest,"rax");}else{if(i.args.size()>6){diagnostics.error("NQR-N5004",{},"native calls currently support at most 6 arguments");return false;}for(std::size_t a=0;a<i.args.size();++a)load(i.args[a],args[a]);out<<"    call "<<symbolForCall(symbols,i.text)<<"\n";if(i.dest)store(*i.dest,"rax");}break;}
        case NirOp::Jump:out<<"    jmp "<<pcLabel(fn,i.target)<<(avx2JumpRedirects.count(pc)?"_scalar":"")<<"\n";break;
        case NirOp::JumpIfFalse:load(i.args[0],"rax");out<<"    test rax, rax\n    jz "<<pcLabel(fn,i.target)<<"\n";break;
        case NirOp::Return:if(!avx2Loops.empty()&&!keepPerLoopVzeroupper)out<<"    vzeroupper\n";if(!i.args.empty())load(i.args[0],"rax");else out<<"    xor eax, eax\n";restoreSaved();out<<"    leave\n    ret\n";break;
        case NirOp::Nop:break;
    }}
    if(!coldTrapLabels.empty()||!coldBoundsTrapLabels.empty()){
        // If malformed NIR ever reaches the end without a Return, this
        // unconditional branch keeps valid fallthrough out of the trap
        // island. Normal functions have already returned here.
        const auto done=localLabel(fn,fn.code.size(),"cold_traps_end");
        out<<"    jmp "<<done<<"\n";
        for(const auto& name:coldTrapLabels)
            out<<name<<":\n    ud2\n";
        for(const auto& name:coldBoundsTrapLabels)
            out<<name<<":\n    jmp noqeri_rt_bounds_trap\n    ud2\n";
        out<<done<<":\n";
    }
    return true;
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
    // Index call targets once rather than scanning every function per callsite.
    // emplace keeps the previous first-match behavior for duplicate symbols.
    std::unordered_map<std::string,std::string> callSymbols;
    callSymbols.reserve(p.functions.size());
    for(const auto& f:p.functions)
        callSymbols.emplace(f.name,(f.isExtern||f.isExport)?
            safe(f.name):internalSymbol(f.name));
    out<<".intel_syntax noprefix\n.text\n";emitRuntime(out);
    if(!emitFunction(out,p.entry,returns,strings,callSymbols,diagnostics))return false;
    for(const auto&f:p.functions)
        if(!emitFunction(out,f,returns,strings,callSymbols,diagnostics))return false;
    out<<"\n.global noqeri_entry\nnoqeri_entry:\n    mov QWORD PTR [rip + noqeri_abi_current], rdi\n    sub rsp, 8\n    call "<<internalSymbol("__entry")<<"\n    add rsp, 8\n    ret\n";
    out<<"\n.data\n.L_noqeri_newline: .byte 10\n.L_noqeri_float_format: .asciz \"%.6f\"\n.L_noqeri_true: .asciz \"true\"\n.L_noqeri_false_text: .asciz \"false\"\n.L_noqeri_standalone: .asciz \"freestanding\"\n.L_noqeri_bounds_err: .asciz \"NQR-R4000: runtime error: index out of bounds\\n\"\n.L_noqeri_avx2_state: .byte 0\n.p2align 5\n.L_noqeri_vec_iota4: .quad 0, 1, 2, 3\n.L_noqeri_vec_step4: .quad 4, 4, 4, 4\n.L_noqeri_vec_step8: .quad 8, 8, 8, 8\n";
    for(const auto&[value,label]:strings){out<<label<<": .byte ";for(std::size_t i=0;i<value.size();++i){if(i)out<<", ";out<<static_cast<unsigned int>(static_cast<unsigned char>(value[i]));}if(!value.empty())out<<", ";out<<"0\n";}
#if !defined(_WIN32)
    // ELF x86-64 native objects must not request an executable stack.
    out<<"\n.section .note.GNU-stack,\"\",@progbits\n";
#endif
    return !diagnostics.hasErrors();
}
} // namespace noe
