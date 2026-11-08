#include "compiler.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
noe::NirInstruction value(noe::NirOp op, noe::Reg reg, const char* name="") {
    noe::NirInstruction in;
    in.op=op;
    in.dest=reg;
    in.text=name;
    in.type.kind=noe::TypeKind::Int;
    return in;
}
noe::NirInstruction binary(noe::Reg dest,noe::Reg a,noe::Reg b) {
    auto in=value(noe::NirOp::Binary,dest,"+");
    in.args={a,b};
    return in;
}
noe::NirInstruction store(noe::Reg reg) {
    noe::NirInstruction in;
    in.op=noe::NirOp::Store;
    in.text="sink";
    in.args={reg};
    return in;
}
noe::NirInstruction ret(noe::Reg reg) {
    noe::NirInstruction in;
    in.op=noe::NirOp::Return;
    in.args={reg};
    return in;
}
bool check(bool ok,const char* reason) {
    if(!ok)std::cerr<<"gvn-regression: "<<reason<<"\n";
    return ok;
}
}

int main() {
    const std::size_t duplicates=128;
    noe::NirProgram p;
    auto& fn=p.entry;
    fn.name="__entry";
    fn.nextReg=static_cast<noe::Reg>(duplicates+3);
    fn.registerTypes.resize(fn.nextReg);
    for(auto& type:fn.registerTypes)type.kind=noe::TypeKind::Int;
    fn.code.reserve(2+duplicates*2+1);
    fn.code.push_back(value(noe::NirOp::Load,1,"a"));
    fn.code.push_back(value(noe::NirOp::Load,2,"b"));
    for(std::size_t i=0;i<duplicates;++i) {
        const auto dest=static_cast<noe::Reg>(3+i);
        fn.code.push_back(binary(dest,1,2));
        fn.code.push_back(store(dest));
    }
    fn.code.push_back(ret(3));
    noe::Optimizer{}.optimize(p,noe::OptimizationLevel::O2);

    std::size_t arithmetic=0,stores=0;
    bool storesMatch=true;
    for(const auto& in:p.entry.code){
        if(in.op==noe::NirOp::Binary&&in.text=="+")++arithmetic;
        if(in.op==noe::NirOp::Store&&in.text=="sink"){
            ++stores;
            storesMatch &= in.args.size()==1&&in.args.front()==3;
        }
    }
    if(!check(arithmetic==1,"exactly one canonical arithmetic op retained")||
       !check(stores==duplicates,"preserve all side-effecting stores")||
       !check(storesMatch,"all stores use the canonical value"))return 1;

    // Cross-basic-block uses must prevent elimination when the replacement
    // would require a global SSA rewrite instead of local-forward aliases.
    noe::NirProgram cross;
    auto& c=cross.entry;
    c.name="__entry";
    c.nextReg=5;
    c.registerTypes.resize(5);
    for(auto& t:c.registerTypes)t.kind=noe::TypeKind::Int;
    c.code={value(noe::NirOp::Load,1,"a"),
            value(noe::NirOp::Load,2,"b"),
            binary(3,1,2),binary(4,1,2)};
    noe::NirInstruction jump;
    jump.op=noe::NirOp::Jump;
    jump.target=5;
    c.code.push_back(jump);
    c.code.push_back(store(4));
    c.code.push_back(ret(4));
    noe::Optimizer{}.optimize(c,noe::OptimizationLevel::O2);
    bool crossStore=false,hasFour=false;
    for(const auto& in:c.code){
        if(in.op==noe::NirOp::Store&&in.args.size()==1&&in.args[0]==4)
            crossStore=true;
        if(in.op==noe::NirOp::Binary&&in.dest&&*in.dest==4)
            hasFour=true;
    }
    if(!check(crossStore&&hasFour,"retain cross-block expression definitions"))
        return 1;
    std::cout<<"PASS: local GVN equivalence and cross-block safety\n";
    return 0;
}
