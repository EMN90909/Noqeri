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
    if(!criticalFall&&flag){
        // The value defined exclusively on the other branch is NOT
        // available to the predecessor that takes the critical edge.
        auto invalid=fn;
        auto wrong=phi;
        wrong.incoming={{0,3},{1,2}};
        if(noe::lowerPhiNodes(invalid,{wrong})||
           invalid.code.size()!=fn.code.size()){
            std::cerr<<"unsound Phi predecessor was accepted\n";
            std::exit(1);
        }
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
noe::NirFunction swapLoop(const std::string& name,std::int64_t limit){
    // Simultaneous backedge assignment: (a,b) := (b,a). Without
    // temporary staging, sequential edge copies would clobber b.
    noe::NirFunction fn;
    fn.name=name;fn.isExport=true;
    fn.resultType.kind=noe::TypeKind::U64;
    fn.registerTypes.resize(14);
    for(noe::Reg reg=1;reg<14;++reg)
        fn.registerTypes[reg].kind=noe::TypeKind::U64;
    fn.registerTypes[8].kind=noe::TypeKind::Bool;
    fn.nextReg=14;
    auto ins=[](noe::NirOp op,std::optional<noe::Reg> dest,
                std::initializer_list<noe::Reg> args={}){
        noe::NirInstruction in;
        in.op=op;in.dest=dest;
        in.args.assign(args.begin(),args.end());
        in.type.kind=noe::TypeKind::U64;
        return in;
    };
    auto initialize=ins(noe::NirOp::Store,std::nullopt,{3});
    initialize.text="i";
    auto current=ins(noe::NirOp::Load,6);
    current.text="i";
    auto cmp=ins(noe::NirOp::Binary,8,{6,7});
    cmp.text="<";cmp.type.kind=noe::TypeKind::Bool;
    auto guard=ins(noe::NirOp::JumpIfFalse,std::nullopt,{8});
    guard.target=13;
    auto increment=ins(noe::NirOp::Binary,10,{6,9});
    increment.text="+";
    auto store=ins(noe::NirOp::Store,std::nullopt,{10});
    store.text="i";
    auto back=ins(noe::NirOp::Jump,std::nullopt);
    back.target=4;
    auto scaled=ins(noe::NirOp::Binary,12,{4,11});
    scaled.text="*";
    auto result=ins(noe::NirOp::Binary,13,{12,5});
    result.text="+";
    fn.code={
        literal(1,10),literal(2,20),literal(3,0),initialize,
        ins(noe::NirOp::Nop,std::nullopt),current,literal(7,limit),
        cmp,guard,literal(9,1),increment,store,back,
        literal(11,10),scaled,result,
        ins(noe::NirOp::Return,std::nullopt,{13})
    };
    noe::NirPhiNode a,b;
    a.block=b.block=1;
    a.dest=4;b.dest=5;
    a.type.kind=b.type.kind=noe::TypeKind::U64;
    a.incoming={{0,1},{2,5}};
    b.incoming={{0,2},{2,4}};
    if(!noe::lowerPhiNodes(fn,{a,b})){
        std::cerr<<"parallel Phi cycle lowering failed\n";
        std::exit(1);
    }
    unsigned saves=0,writes=0;
    for(const auto& in:fn.code){
        if(in.op!=noe::NirOp::Cast||!in.dest)continue;
        if(*in.dest==4||*in.dest==5)++writes;
        if(*in.dest>=14)++saves;
    }
    if(saves!=4||writes!=4){
        std::cerr<<"parallel Phi cycle lacks staging\n";
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
    p.functions.push_back(swapLoop("phi_swap_one",1));
    p.functions.push_back(swapLoop("phi_swap_two",2));
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)){
        for(const auto& error:diagnostics.items())
            std::cerr<<error.code<<": "<<error.message<<"\n";
        return 1;
    }
    return 0;
}
