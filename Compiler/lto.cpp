#include "lto.hpp"
#include <unordered_set>

namespace noe {
LinkTimeResult combineNirModules(
    const NirProgram& root, const std::vector<NirProgram>& imports) {
    LinkTimeResult result;
    result.program=root;
    std::unordered_set<std::string> names;
    auto validate=[&](const NirFunction& fn)->bool {
        if(fn.name.empty()){
            result.error="NIR link rejects anonymous function definitions";
            return false;
        }
        if(!names.insert(fn.name).second){
            result.error="NIR link duplicate function: "+fn.name;
            return false;
        }
        for(const auto& in:fn.code) {
            if((in.op==NirOp::Jump||in.op==NirOp::JumpIfFalse)&&
                in.target>fn.code.size()){
                result.error="NIR link invalid branch target in: "+fn.name;
                return false;
            }
        }
        return true;
    };
    for(const auto& fn:root.functions)
        if(!validate(fn))return result;
    for(const auto& imported:imports){
        // Module initializers require explicit run-once semantics.
        // Reject rather than silently discard arbitrary top-level effects.
        const auto& initializer=imported.entry.code;
        if(!initializer.empty() &&
           !(initializer.size()==1&&initializer[0].op==NirOp::Return&&
             initializer[0].args.empty())){
            result.error="NIR link rejects imported module top-level effects";
            return result;
        }
        for(const auto& fn:imported.functions){
            if(!validate(fn))return result;
            result.program.functions.push_back(fn);
            ++result.importedFunctions;
        }
    }
    result.ok=true;
    return result;
}
