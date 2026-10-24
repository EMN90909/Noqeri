#include "noe.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
noe::NirInstruction ins(noe::NirOp op,noe::Reg dest=0,
                         noe::TypeKind type=noe::TypeKind::U64){
    noe::NirInstruction in;
    in.op=op;
    if(dest)in.dest=dest;
    in.type.kind=type;
    return in;
}
noe::NirInstruction value(noe::Reg dest,std::int64_t v){
    auto in=ins(noe::NirOp::Const,dest);
    in.literal=v;
    return in;
}
noe::NirInstruction args(noe::NirOp op,noe::Reg dest,
                         std::initializer_list<noe::Reg> a){
    auto in=ins(op,dest);
    in.args.assign(a.begin(),a.end());
    return in;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry.name="__entry";
    p.entry.code={ins(noe::NirOp::Return)};
    noe::NirFunction fn;
    fn.name="simd_loop";fn.isExport=true;
    fn.resultType.kind=noe::TypeKind::U64;
    fn.registerTypes.resize(20);
    for(noe::Reg r=1;r<20;++r)
        fn.registerTypes[r].kind=noe::TypeKind::U64;
    fn.registerTypes[11].kind=noe::TypeKind::Bool;
    fn.nextReg=20;
    auto allocate=ins(noe::NirOp::StackAlloc,1);
    allocate.width=16;
    auto ptr0=args(noe::NirOp::PtrOffset,4,{1,2});ptr0.width=8;
    auto ptr1=args(noe::NirOp::PtrOffset,5,{1,3});ptr1.width=8;
    auto st0=args(noe::NirOp::StoreMemory,0,{4,6});st0.width=8;
    auto st1=args(noe::NirOp::StoreMemory,0,{5,6});st1.width=8;
    auto initialize=args(noe::NirOp::Store,0,{9});initialize.text="i";
    auto loopIndex=ins(noe::NirOp::Load,10);loopIndex.text="i";
    auto cmp=args(noe::NirOp::Binary,11,{10,8});
    cmp.text="<";cmp.type.kind=noe::TypeKind::Bool;
    auto guard=args(noe::NirOp::JumpIfFalse,0,{11});
    guard.target=24;
    auto ld0=args(noe::NirOp::LoadMemory,12,{4});ld0.width=8;
    auto ld1=args(noe::NirOp::LoadMemory,13,{5});ld1.width=8;
    auto add0=args(noe::NirOp::Binary,14,{12,7});add0.text="+";
    auto add1=args(noe::NirOp::Binary,15,{13,7});add1.text="+";
    auto store0=args(noe::NirOp::StoreMemory,0,{4,14});store0.width=8;
    auto store1=args(noe::NirOp::StoreMemory,0,{5,15});store1.width=8;
    auto advance=args(noe::NirOp::Binary,16,{10,7});advance.text="+";
    auto setIndex=args(noe::NirOp::Store,0,{16});setIndex.text="i";
    auto back=ins(noe::NirOp::Jump);back.target=12;
    auto out0=args(noe::NirOp::LoadMemory,17,{4});out0.width=8;
    auto out1=args(noe::NirOp::LoadMemory,18,{5});out1.width=8;
    auto sum=args(noe::NirOp::Binary,19,{17,18});sum.text="+";
    auto ret=args(noe::NirOp::Return,0,{19});
    fn.code={
        allocate,value(2,0),value(3,1),ptr0,ptr1,value(6,0),
        st0,st1,value(7,1),value(8,5000000),value(9,0),
        initialize,loopIndex,cmp,guard,ld0,ld1,add0,add1,
        store0,store1,advance,setIndex,back,out0,out1,sum,ret
    };
    p.functions.push_back(fn);
    noe::Diagnostics diag;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diag)){
        for(const auto& err:diag.items())
            std::cerr<<err.code<<": "<<err.message<<"\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const char* env=std::getenv("NOQERI_EXPERIMENTAL_LOOP_SIMD");
    const bool enabled=env&&std::string(env)=="1";
    const bool present=text.find("movdqu xmm0, XMMWORD PTR [rax]")!=
        std::string::npos&&text.find("paddq xmm0, xmm1")!=
        std::string::npos;
    if(present!=enabled){
        std::cerr<<"loop memory SIMD lowering mismatch\n";
        return 1;
    }
    return 0;
}
