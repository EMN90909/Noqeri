#include "compiler.hpp"
#include "lto.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
noe::NirInstruction literal(noe::Reg r,std::int64_t v){
    noe::NirInstruction op;op.op=noe::NirOp::Const;op.dest=r;op.literal=v;return op;
}
noe::NirInstruction binary(noe::Reg dst,noe::Reg a,noe::Reg b){
    noe::NirInstruction op;op.op=noe::NirOp::Binary;op.dest=dst;
    op.args={a,b};op.text="+";op.type.kind=noe::TypeKind::Int;return op;
}
noe::NirInstruction returning(noe::Reg r){
    noe::NirInstruction op;op.op=noe::NirOp::Return;op.args={r};return op;
}
bool check(bool condition,const char* message){
    if(!condition)std::cerr<<"lto-ir: "<<message<<" failed\n";
    return condition;
}
}
int main(){
    noe::NirProgram root,module;
    noe::NirFunction fn;
    fn.name="importedAnswer";
    fn.code={literal(1,19),literal(2,23),binary(3,1,2),returning(3)};
    module.functions.push_back(fn);
    noe::NirInstruction call;
    call.op=noe::NirOp::Call;call.dest=5;call.text="importedAnswer";
    root.entry.code={call,returning(5)};
    auto linked=noe::combineNirModules(root,{module});
    if(!check(linked.ok&&linked.importedFunctions==1,"combines imported IR function"))return 1;
    noe::PassManager(noe::OptimizationLevel::O3).optimize(linked.program);
    const auto& result=linked.program.entry.code.front();
    if(!check(result.op==noe::NirOp::Const&&
              std::get<std::int64_t>(result.literal)==42,
              "inter-module constant-return evaluation"))return 1;
    module.functions.push_back(fn);
    const auto duplicate=noe::combineNirModules(root,{module});
    if(!check(!duplicate.ok&&duplicate.error.find("duplicate")!=std::string::npos,
              "rejects duplicate link symbols"))return 1;
    module.functions.pop_back();
    module.entry.code={literal(7,999),returning(7)};
    const auto impure=noe::combineNirModules(root,{module});
    if(!check(!impure.ok&&impure.error.find("top-level")!=std::string::npos,
              "refuses to drop imported module initializer"))return 1;
    return 0;
}
