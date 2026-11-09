#include "noe.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

namespace {
noe::NirInstruction literal(noe::Reg dest, std::int64_t n) {
    noe::NirInstruction ins;
    ins.op=noe::NirOp::Const;
    ins.dest=dest;
    ins.literal=n;
    ins.type.kind=noe::TypeKind::I64;
    return ins;
}
noe::NirInstruction binary(noe::Reg dest,const char* operation,
                             noe::Reg lhs,noe::Reg rhs,
                             noe::TypeKind kind=noe::TypeKind::I64) {
    noe::NirInstruction ins;
    ins.op=noe::NirOp::Binary;
    ins.dest=dest;
    ins.text=operation;
    ins.args={lhs,rhs};
    ins.type.kind=kind;
    return ins;
}
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    noe::NirProgram program;
    auto& fn=program.entry;
    fn.name="__entry";
    fn.nextReg=16;
    fn.registerTypes.resize(16);
    for(noe::Reg r=1;r<16;++r)fn.registerTypes[r].kind=noe::TypeKind::I64;
    fn.registerTypes[11].kind=noe::TypeKind::Bool;
    fn.code={
        literal(1,9),
        literal(2,3),
        binary(3,"+",1,2),
        literal(4,-1),
        binary(5,"-",3,4),
        literal(6,7),
        binary(7,"*",5,6),
        literal(8,2147483648LL), // outside signed imm32 range
        binary(9,"+",7,8),
        literal(10,4),
        binary(11,"<",9,10,noe::TypeKind::Bool)
    };
    noe::NirInstruction guard;
    guard.op=noe::NirOp::JumpIfFalse;
    guard.args={11};
    guard.target=13;
    fn.code.push_back(guard);
    // Row-stride scaling in 16x16 matrix kernels must use SHL, not IMUL.
    fn.code.push_back(literal(12,16));
    fn.code.push_back(binary(13,"*",1,12));
    fn.code.push_back(literal(14,0));
    fn.code.push_back(binary(15,"*",1,14));
    fn.code[11].target=17;
    noe::NirInstruction ret;
    ret.op=noe::NirOp::Return;
    ret.args={9};
    fn.code.push_back(ret);
    ret.args={7};
    fn.code.push_back(ret);

    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(program,argv[1],diagnostics)){
        for(const auto& error:diagnostics.items())
            std::cerr<<error.code<<": "<<error.message<<"\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    if(!file)return 1;
    const std::string assembly((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    for(const auto* pattern:{
            R"(add (rax|r1[2-5]|rbx|rsi|rdi|r9), 3)",
            R"(sub (rax|r1[2-5]|rbx|rsi|rdi|r9), -1)",
            R"(imul (rax|r1[2-5]|rbx|rsi|rdi|r9), \1, 7)",
            R"(cmp (rax|r1[2-5]|rbx|rsi|rdi|r9), 4)",
            R"(shl (rax|r1[2-5]|rbx|rsi|rdi|r9), 4)",
            R"(xor (eax|rax|r1[2-5]|rbx|rsi|rdi|r9), \1)"}) {
        if(!std::regex_search(assembly,std::regex(pattern))) {
            std::cerr<<"missing immediate/fallback assembly: "<<pattern<<"\n";
            return 1;
        }
    }
    // Non-imm32 operands now use the allocated register or spill slot
    // directly. Do not force an unnecessary R10 staging copy.
    if(!std::regex_search(assembly,
        std::regex(R"(add (rax|r1[2-5]|rbx|rsi|rdi|r9), (r1[2-5]|rbx|rsi|rdi|r9|QWORD PTR \[rbp-[0-9]+\]))"))){
        std::cerr<<"missing direct register/memory ALU fallback\n";
        return 1;
    }
    return 0;
}
