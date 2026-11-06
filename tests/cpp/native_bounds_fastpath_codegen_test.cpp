#include "noe.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry.name="__entry";
    p.entry.nextReg=5;
    p.entry.registerTypes.resize(5);
    for(noe::Reg r=1;r<5;++r)
        p.entry.registerTypes[r].kind=noe::TypeKind::U64;
    noe::NirInstruction index;
    index.op=noe::NirOp::Const;
    index.dest=1;
    index.literal=std::int64_t{7};
    noe::NirInstruction length=index;
    length.dest=2;
    length.literal=std::int64_t{8};
    noe::NirInstruction bound;
    bound.op=noe::NirOp::CheckBounds;
    bound.args={1,2};
    noe::NirInstruction ret;
    ret.op=noe::NirOp::Return;
    ret.args={1};
    p.entry.code={index,length,bound,ret};
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)) {
        for(const auto& e:diagnostics.items())
            std::cerr<<e.code<<": "<<e.message<<"\n";
        return 1;
    }
    std::ifstream stream(argv[1]);
    if(!stream)return 1;
    const std::string asmText((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    // The constant bounds length must compare directly against a physical
    // register rather than staging the value through RAX. Keep the trap.
    if(!std::regex_search(asmText,std::regex(R"(cmp r1[2-5], 8\n\s+jb )"))||
       asmText.find("ud2")==std::string::npos) {
        std::cerr<<"missing direct fixed-length CMP and trap\n";
        return 1;
    }
    return 0;
}
