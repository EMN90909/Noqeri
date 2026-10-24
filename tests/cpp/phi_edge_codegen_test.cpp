#include "noe.hpp"
#include "optimizer_cost.hpp"
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
noe::NirInstruction literal(noe::Reg dest,std::int64_t value,
                            noe::TypeKind kind=noe::TypeKind::U64){
    noe::NirInstruction in;
    in.op=noe::NirOp::Const;in.dest=dest;
    in.type.kind=kind;in.literal=value;
    return in;
}
noe::NirFunction function(const std::string& name,bool criticalFall,
                          bool flag){
    noe::NirFunction fn;
    fn.name=name;
    fn.isExport=true;
    fn.resultType.kind=noe::TypeKind::U64;
    fn.registerTypes.resize(5);
    fn.registerTypes[1].kind=noe::TypeKind::Bool;
    for(noe::Reg r:{2u,3u,4u})
        fn.registerTypes[r].kind=noe::TypeKind::U64;
    fn.nextReg=5;
    auto conditional=noe::NirInstruction{};
    conditional.op=noe::NirOp::JumpIfFalse;
    conditional.args={1};conditional.target=5;
    auto jump=noe::NirInstruction{};
    jump.op=noe::NirOp::Jump;
    auto ret=noe::NirInstruction{};
    ret.op=noe::NirOp::Return;ret.args={4};
    noe::NirPhiNode phi;
    phi.dest=4;phi.type.kind=noe::TypeKind::U64;
    if(!criticalFall){
        jump.target=5;
        fn.code={literal(2,7),literal(1,flag?1:0,noe::TypeKind::Bool),
                 conditional,literal(3,12),jump,
                 noe::NirInstruction{},ret};
        phi.block=2;
        phi.incoming={{0,2},{1,3}};
    }else{
        jump.target=3;
        fn.code={literal(2,7),literal(1,flag?1:0,noe::TypeKind::Bool),
                 conditional,noe::NirInstruction{},ret,
                 literal(3,12),jump};
        phi.block=1;
        phi.incoming={{0,2},{2,3}};
    }
    if(!noe::lowerPhiNodes(fn,{phi})){
        std::cerr<<"Phi lowering failed for "<<name<<"\n";
        std::exit(1);
    }
    unsigned casts=0;
    for(const auto& in:fn.code)
        if(in.op==noe::NirOp::Cast&&in.dest&&*in.dest==4)++casts;
    if(casts!=2){
        std::cerr<<"wrong Phi edge copy count for "<<name<<"\n";
        std::exit(1);
    }
    return fn;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry.name="__entry";
    noe::NirInstruction entryReturn;
    entryReturn.op=noe::NirOp::Return;
    p.entry.code={entryReturn};
    p.functions.push_back(function("phi_jump_true",false,true));
    p.functions.push_back(function("phi_jump_false",false,false));
    p.functions.push_back(function("phi_fall_true",true,true));
    p.functions.push_back(function("phi_fall_false",true,false));
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)){
        for(const auto& error:diagnostics.items())
            std::cerr<<error.code<<": "<<error.message<<"\n";
        return 1;
    }
    return 0;
}
