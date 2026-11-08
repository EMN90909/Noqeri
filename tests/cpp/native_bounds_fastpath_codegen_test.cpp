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
    if(!std::regex_search(asmText,std::regex(R"(cmp r1[2-5], 8\n\s+jae )"))||
       asmText.find("ud2")==std::string::npos||
       asmText.find("cold_traps_end")==std::string::npos) {
        std::cerr<<"missing direct fixed-length CMP and trap\n";
        return 1;
    }
    // A dynamic unsigned index needs only a single unsigned comparison.
    // A signed index must still trap negatives before the upper-bound test.
    noe::NirInstruction dynamicIndex;
    dynamicIndex.op=noe::NirOp::Load;
    dynamicIndex.dest=1;
    dynamicIndex.text="runtimeIndex";
    noe::NirInstruction dynamicLength=dynamicIndex;
    dynamicLength.dest=2;
    dynamicLength.text="runtimeLength";
    p.entry.code={dynamicIndex,dynamicLength,bound,ret};
    p.entry.registerTypes[2].kind=noe::TypeKind::Usize;
    auto dynamicAsm=[&](noe::TypeKind indexKind)->std::string {
        p.entry.registerTypes[1].kind=indexKind;
        noe::Diagnostics errors;
        if(!noe::NativeBackend{}.emitAssembly(p,argv[1],errors))
            return {};
        std::ifstream file(argv[1]);
        if(!file)return {};
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };
    const auto unsignedAsm=dynamicAsm(noe::TypeKind::Usize);
    if(unsignedAsm.empty()||
       !std::regex_search(unsignedAsm,
           std::regex(R"(cmp [^\n]+\n\s+jae )"))||
       unsignedAsm.find("    js ")!=std::string::npos){
        std::cerr<<"dynamic unsigned bounds must use one unsigned CMP/JAE guard\n";
        return 1;
    }
    const auto signedAsm=dynamicAsm(noe::TypeKind::I64);
    if(signedAsm.empty()||
       signedAsm.find("    js ")==std::string::npos||
       signedAsm.find("    jae ")==std::string::npos){
        std::cerr<<"dynamic signed bounds lost the negative-index trap\n";
        return 1;
    }
    return 0;
}
