#include "noe.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
noe::NirInstruction value(noe::Reg dest,std::int64_t n){
    noe::NirInstruction in;
    in.op=noe::NirOp::Const;
    in.dest=dest;
    in.literal=n;
    in.type.kind=noe::TypeKind::U64;
    return in;
}
noe::NirInstruction op(noe::Reg dest,const char* text,
                       noe::Reg a,noe::Reg b){
    noe::NirInstruction in;
    in.op=noe::NirOp::Binary;
    in.dest=dest;
    in.text=text;
    in.args={a,b};
    in.type.kind=noe::TypeKind::U64;
    return in;
}
noe::NirFunction makeFunction(const std::string& name,
                            const char* pairOp,
                            std::int64_t a,std::int64_t b,
                            std::int64_t c,std::int64_t d){
    noe::NirFunction fn;
    fn.name=name;
    fn.nextReg=8;
    fn.resultType.kind=noe::TypeKind::U64;
    fn.registerTypes.resize(8);
    for(noe::Reg r=1;r<8;++r)
        fn.registerTypes[r].kind=noe::TypeKind::U64;
    fn.code={value(1,a),value(2,b),value(3,c),value(4,d),
             op(5,pairOp,1,2),op(6,pairOp,3,4),op(7,"+",5,6)};
    noe::NirInstruction ret;
    ret.op=noe::NirOp::Return;
    ret.args={7};
    fn.code.push_back(ret);
    return fn;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry=makeFunction("__entry","+",10,20,3,4);
    auto subtract=makeFunction("simd_sub","-",40,8,11,4);
    subtract.isExport=true;
    p.functions.push_back(subtract);
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)){
        for(const auto& error:diagnostics.items())
            std::cerr<<error.code<<": "<<error.message<<"\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    if(!file)return 1;
    const std::string assembly((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if(assembly.find("paddq xmm0, xmm2")==std::string::npos||
       assembly.find("psubq xmm0, xmm2")==std::string::npos){
        std::cerr<<"experimental SSE2 packed operations absent\n";
        return 1;
    }
    return 0;
}
