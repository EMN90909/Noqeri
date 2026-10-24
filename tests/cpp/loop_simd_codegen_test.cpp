#include "noe.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
noe::NirInstruction instruction(noe::NirOp op,noe::Reg dest=0,
                                  std::vector<noe::Reg> args={}){
    noe::NirInstruction in;
    in.op=op;in.args=std::move(args);
    if(dest)in.dest=dest;
    in.type.kind=noe::TypeKind::U64;
    return in;
}
noe::NirInstruction literal(noe::Reg dest,std::int64_t value){
    auto in=instruction(noe::NirOp::Const,dest);
    in.literal=value;
    return in;
}
struct Pair {noe::Reg left,right;};
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry.name="__entry";
    p.entry.code={instruction(noe::NirOp::Return)};
    noe::NirFunction fn;
    fn.name="simd_loop";fn.isExport=true;
    fn.resultType.kind=noe::TypeKind::U64;
    noe::Reg next=1;
    auto reg=[&](){return next++;};
    const noe::Reg zero=reg(),one=reg(),iterations=reg();
    fn.code.push_back(literal(zero,0));
    fn.code.push_back(literal(one,1));
    fn.code.push_back(literal(iterations,100000));
    constexpr int kPairs=32;
    std::vector<Pair> pairs;
    for(int i=0;i<kPairs;++i){
        const auto root=reg(),left=reg(),right=reg();
        auto alloc=instruction(noe::NirOp::StackAlloc,root);
        alloc.width=16;
        auto ptrL=instruction(noe::NirOp::PtrOffset,left,{root,zero});
        auto ptrR=instruction(noe::NirOp::PtrOffset,right,{root,one});
        ptrL.width=ptrR.width=8;
        auto initializeL=instruction(noe::NirOp::StoreMemory,0,{left,zero});
        auto initializeR=instruction(noe::NirOp::StoreMemory,0,{right,zero});
        initializeL.width=initializeR.width=8;
        fn.code.insert(fn.code.end(),
            {alloc,ptrL,ptrR,initializeL,initializeR});
        pairs.push_back({left,right});
    }
    auto init=instruction(noe::NirOp::Store,0,{zero});
    init.text="i";
    fn.code.push_back(init);
    const std::size_t header=fn.code.size();
    const auto index=reg(),compare=reg();
    auto readIndex=instruction(noe::NirOp::Load,index);
    readIndex.text="i";
    auto test=instruction(noe::NirOp::Binary,compare,{index,iterations});
    test.text="<";test.type.kind=noe::TypeKind::Bool;
    auto guard=instruction(noe::NirOp::JumpIfFalse,0,{compare});
    const std::size_t guardPc=fn.code.size()+2;
    fn.code.insert(fn.code.end(),{readIndex,test,guard});
    for(const auto& pair:pairs){
        const auto a=reg(),b=reg(),sumA=reg(),sumB=reg();
        auto la=instruction(noe::NirOp::LoadMemory,a,{pair.left});
        auto lb=instruction(noe::NirOp::LoadMemory,b,{pair.right});
        auto aa=instruction(noe::NirOp::Binary,sumA,{a,one});
        auto ab=instruction(noe::NirOp::Binary,sumB,{b,one});
        aa.text=ab.text="+";
        auto sa=instruction(noe::NirOp::StoreMemory,0,{pair.left,sumA});
        auto sb=instruction(noe::NirOp::StoreMemory,0,{pair.right,sumB});
        la.width=lb.width=sa.width=sb.width=8;
        fn.code.insert(fn.code.end(),{la,lb,aa,ab,sa,sb});
    }
    const auto incremented=reg();
    auto addIndex=instruction(noe::NirOp::Binary,incremented,{index,one});
    addIndex.text="+";
    auto update=instruction(noe::NirOp::Store,0,{incremented});
    update.text="i";
    auto back=instruction(noe::NirOp::Jump);
    back.target=header;
    fn.code.insert(fn.code.end(),{addIndex,update,back});
    fn.code[guardPc].target=fn.code.size();
    auto accumulator=reg();
    fn.code.push_back(literal(accumulator,0));
    for(const auto& pair:pairs){
        for(const auto ptr:{pair.left,pair.right}){
            const auto loaded=reg(),added=reg();
            auto load=instruction(noe::NirOp::LoadMemory,loaded,{ptr});
            load.width=8;
            auto plus=instruction(noe::NirOp::Binary,added,
                                  {accumulator,loaded});
            plus.text="+";
            fn.code.push_back(load);
            fn.code.push_back(plus);
            accumulator=added;
        }
    }
    fn.code.push_back(instruction(noe::NirOp::Return,0,{accumulator}));
    fn.registerTypes.resize(next);
    for(noe::Reg r=1;r<next;++r)
        fn.registerTypes[r].kind=noe::TypeKind::U64;
    fn.registerTypes[compare].kind=noe::TypeKind::Bool;
    fn.nextReg=next;
    p.functions.push_back(fn);
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)){
        for(const auto& err:diagnostics.items())
            std::cerr<<err.code<<": "<<err.message<<"\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const char* env=std::getenv("NOQERI_EXPERIMENTAL_LOOP_SIMD");
    const bool enabled=env&&std::string(env)=="1";
    unsigned count=0;
    std::size_t indexAt=0;
    while((indexAt=text.find("paddq xmm0, xmm1",indexAt))!=
          std::string::npos){++count;indexAt+=5;}
    if((enabled&&count!=kPairs)||(!enabled&&count!=0)){
        std::cerr<<"expected "<<(enabled?kPairs:0)<<" SIMD pairs, got "<<count<<"\n";
        return 1;
    }
    return 0;
}
