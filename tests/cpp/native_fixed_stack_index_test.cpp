#include "noe.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace {
noe::NirInstruction op(noe::NirOp kind,noe::Reg dest=0,
                        std::vector<noe::Reg> args={}) {
    noe::NirInstruction in; in.op=kind; in.args=std::move(args);
    if(dest)in.dest=dest;
    return in;
}
noe::NirFunction make(const std::string& name,bool rebind,bool addressTaken) {
    noe::NirFunction fn;fn.name=name;fn.isExport=true;
    fn.params={"index"};
    fn.paramTypes.resize(1);fn.paramTypes[0].kind=noe::TypeKind::Usize;
    fn.resultType.kind=noe::TypeKind::Int;
    fn.registerTypes.resize(16);fn.nextReg=16;
    for(auto& type:fn.registerTypes)type.kind=noe::TypeKind::Int;
    for(noe::Reg r:{1u,2u,6u,7u,11u,12u,15u})
        fn.registerTypes[r].kind=noe::TypeKind::Pointer;
    fn.registerTypes[3].kind=noe::TypeKind::Usize;
    fn.registerTypes[9].kind=noe::TypeKind::Usize;
    auto alloc=op(noe::NirOp::StackAlloc,1);alloc.width=256*8;
    auto bind=op(noe::NirOp::Store,0,{1});bind.text="values";
    fn.code.push_back(alloc);fn.code.push_back(bind);
    if(rebind){
        auto another=op(noe::NirOp::StackAlloc,2);another.width=256*8;
        auto overwrite=op(noe::NirOp::Store,0,{2});
        overwrite.text="values";
        fn.code.push_back(another);fn.code.push_back(overwrite);
    }
    if(addressTaken){
        auto address=op(noe::NirOp::AddressOf,15);
        address.text="values";
        fn.code.push_back(address);
    }
    auto i=op(noe::NirOp::Load,3);i.text="index";
    auto length=op(noe::NirOp::Const,9);
    length.literal=std::int64_t{256};
    auto check=op(noe::NirOp::CheckBounds,0,{3,9});
    auto base=op(noe::NirOp::Load,6);base.text="values";
    auto pointer=op(noe::NirOp::PtrOffset,7,{6,3});pointer.width=8;
    auto value=op(noe::NirOp::Const,10);value.literal=std::int64_t{42};
    auto write=op(noe::NirOp::StoreMemory,0,{7,10});write.width=8;
    auto pointer2=op(noe::NirOp::PtrOffset,11,{6,3});pointer2.width=8;
    auto read=op(noe::NirOp::LoadMemory,13,{11});read.width=8;
    read.type.kind=noe::TypeKind::Int;
    auto ret=op(noe::NirOp::Return,0,{13});
    // PtrOffset must be adjacent to the actual memory access.
    // Store 42, then reload through another adjacent pointer calculation.
    fn.code.insert(fn.code.end(),{i,length,check,base,value,pointer,write,
                                  pointer2,read,ret});
    return fn;
}
noe::NirFunction compareFixedArray() {
    noe::NirFunction fn;
    fn.name="compare_fixed_array";fn.isExport=true;
    fn.params={"index","key"};
    fn.paramTypes.resize(2);
    fn.paramTypes[0].kind=noe::TypeKind::Usize;
    fn.paramTypes[1].kind=noe::TypeKind::Int;
    fn.resultType.kind=noe::TypeKind::Int;
    fn.nextReg=13;fn.registerTypes.resize(13);
    for(auto& type:fn.registerTypes)type.kind=noe::TypeKind::Int;
    for(noe::Reg r:{1u,4u,6u,7u})
        fn.registerTypes[r].kind=noe::TypeKind::Pointer;
    fn.registerTypes[2].kind=noe::TypeKind::Usize;
    fn.registerTypes[10].kind=noe::TypeKind::Bool;
    auto alloc=op(noe::NirOp::StackAlloc,1);alloc.width=256*8;
    auto bind=op(noe::NirOp::Store,0,{1});bind.text="values";
    auto index=op(noe::NirOp::Load,2);index.text="index";
    auto length=op(noe::NirOp::Const,3);length.literal=std::int64_t{256};
    auto check=op(noe::NirOp::CheckBounds,0,{2,3});
    auto base=op(noe::NirOp::Load,4);base.text="values";
    auto val=op(noe::NirOp::Const,5);val.literal=std::int64_t{42};
    auto writePtr=op(noe::NirOp::PtrOffset,6,{4,2});writePtr.width=8;
    auto write=op(noe::NirOp::StoreMemory,0,{6,5});write.width=8;
    auto readPtr=op(noe::NirOp::PtrOffset,7,{4,2});readPtr.width=8;
    auto read=op(noe::NirOp::LoadMemory,8,{7});
    read.width=8;read.type.kind=noe::TypeKind::Int;
    auto key=op(noe::NirOp::Load,9);key.text="key";
    auto greater=op(noe::NirOp::Binary,10,{8,9});
    greater.text=">";greater.type.kind=noe::TypeKind::Bool;
    auto branch=op(noe::NirOp::JumpIfFalse,0,{10});branch.target=16;
    auto yes=op(noe::NirOp::Const,11);yes.literal=std::int64_t{1};
    auto yesReturn=op(noe::NirOp::Return,0,{11});
    auto no=op(noe::NirOp::Const,12);no.literal=std::int64_t{0};
    auto noReturn=op(noe::NirOp::Return,0,{12});
    fn.code={alloc,bind,index,length,check,base,val,writePtr,write,
             readPtr,read,key,greater,branch,yes,yesReturn,no,noReturn};
    return fn;
}
std::string region(const std::string& asmText,const std::string& name,
                   const std::string& next) {
    const auto begin=asmText.find("\n"+name+":");
    if(begin==std::string::npos) return {};
    const auto end=next.empty()?std::string::npos:
        asmText.find("\n"+next+":",begin+name.size()+2);
    return asmText.substr(begin,end==std::string::npos?end:end-begin);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    noe::NirProgram p;
    p.entry.name="__entry";
    p.entry.code={op(noe::NirOp::Return)};
    p.functions.push_back(make("direct_fixed_array",false,false));
    p.functions.push_back(make("rebound_array",true,false));
    p.functions.push_back(make("address_taken_array",false,true));
    p.functions.push_back(compareFixedArray());
    noe::Diagnostics diagnostics;
    if(!noe::NativeBackend{}.emitAssembly(p,argv[1],diagnostics)){
        for(const auto& error:diagnostics.items())
            std::cerr<<error.code<<": "<<error.message<<"\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    const std::string assembly((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fast=region(assembly,"direct_fixed_array","rebound_array");
    const auto rebound=region(assembly,"rebound_array","address_taken_array");
    const auto taken=region(assembly,"address_taken_array","compare_fixed_array");
    const auto cmp=region(assembly,"compare_fixed_array","");
    const std::regex rbpIndexed(
        R"(QWORD PTR \[rbp - [0-9]+ \+ (r11|r12|r13|r14|r15)\*8\])");
    const std::regex registerIndexed(
        R"(QWORD PTR \[(rax|r12|r13|r14|r15) \+ (r11|r12|r13|r14|r15)\*8\])");
    if(fast.empty()||rebound.empty()||taken.empty()||cmp.empty()||
       !std::regex_search(fast,rbpIndexed)||
       std::regex_search(rebound,rbpIndexed)||
       std::regex_search(taken,rbpIndexed)||
       fast.find("ud2")==std::string::npos||
       !std::regex_search(rebound,registerIndexed)||
       !std::regex_search(taken,registerIndexed)||
       cmp.find("cmp QWORD PTR [r8], ")==std::string::npos||
       cmp.find("lea r8, [rbp - ")==std::string::npos) {
        std::cerr<<"fixed stack-index specialization or fallback failed\n";
        return 1;
    }
    return 0;
}
