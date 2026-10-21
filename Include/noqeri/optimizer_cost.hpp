#pragma once
#include "nir.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace noe {
// Explicit heuristic table; not a measurement-backed instruction scheduler.
// Unknown targets conservatively disallow speculative vector transforms.
struct TargetCostModel {
    std::string target;
    std::size_t availableIntegerRegisters=4;
    std::size_t vectorBits=0;
    unsigned intAdd=1, intMultiply=4, intDivide=32, memoryLoad=4, branch=2;
    bool supportsSimd() const { return vectorBits>0; }
    static TargetCostModel forTarget(const std::string& triple) {
        TargetCostModel c; c.target=triple;
        if(triple.find("x86_64")!=std::string::npos || triple.find("amd64")!=std::string::npos) {
            c.availableIntegerRegisters=10; c.vectorBits=128;
            c.intAdd=1; c.intMultiply=3; c.intDivide=25; c.memoryLoad=4; c.branch=3;
        } else if(triple.find("aarch64")!=std::string::npos || triple.find("arm64")!=std::string::npos) {
            c.availableIntegerRegisters=18; c.vectorBits=128;
            c.intAdd=1; c.intMultiply=3; c.intDivide=16; c.memoryLoad=4; c.branch=2;
        } else if(triple.find("wasm32")!=std::string::npos) {
            c.availableIntegerRegisters=6; c.vectorBits=0; // SIMD opt-in requires target features.
        }
        return c;
    }
};
// Analysis output is deliberately separate from proof of optimization legality.
struct OptimizationCapabilities {
    bool uniqueVirtualRegisterDefinitions=false;
    bool allDefinitionsDominateUses=false;
    std::size_t basicBlocks=0;
    std::size_t naturalBackedges=0;
    std::size_t candidateLoopConstants=0;
    std::size_t candidateVectorOperations=0;
    std::size_t nonEscapingStackAllocations=0;
};
OptimizationCapabilities inspectOptimizations(const NirFunction& fn);
// MustAlias means the *same virtual pointer value*, not that two different
// pointer expressions happen to have the same address. Distinct, nonescaping
// StackAlloc roots permit NoAlias only in acyclic NIR. Everything else is May.
enum class AliasRelation { NoAlias, MayAlias, MustAlias };
class NirAliasAnalysis {
public:
    explicit NirAliasAnalysis(const NirFunction& fn);
    AliasRelation query(Reg a, Reg b) const;
    bool escapes(Reg pointer) const;
private:
    std::unordered_map<Reg,Reg> roots_;
    std::unordered_set<Reg> escapingRoots_;
    bool hasBackedge_=false;
    bool ssaValid_=false;
};
} // namespace noe
