#include "noe.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {

std::optional<std::size_t> fixedArrayCount(const std::string&type){
    if(type.size()<4||type.front()!='['||type.rfind("[]",0)==0)return std::nullopt;
    auto semi=type.rfind(';'),close=type.rfind(']');if(semi==std::string::npos||close==std::string::npos||semi>=close)return std::nullopt;
    try{return static_cast<std::size_t>(std::stoull(type.substr(semi+1,close-semi-1),nullptr,0));}catch(...){return std::nullopt;}
}
bool pointerType(const std::string&type){return !type.empty()&&type.front()=='*';}

class Annotator {
public:
    Annotator(Diagnostics&diagnostics,bool allowUnsafe):diagnostics_(diagnostics),allowUnsafe_(allowUnsafe){}
    void function(const std::shared_ptr<FunctionStmt>&fn){
        bounds_.clear();rawPointers_.clear();unsafeDepth_=0;
        for(const auto&p:fn->params)if(p.annotation){if(auto count=fixedArrayCount(*p.annotation))bounds_[p.name]=*count;if(pointerType(*p.annotation))rawPointers_.insert(p.name);}
        if(fn->body)statement(fn->body);
    }

private:
    void requireUnsafe(Span span,const std::string&operation){
        if(allowUnsafe_||unsafeDepth_>0)return;
        diagnostics_.error("NQR-S3100",span,operation+" requires an explicit unsafe block","wrap only the smallest raw-memory operation in unsafe { ... }");
    }

    bool expressionProducesPointer(const ExprPtr&e)const{
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(e))return unary->op==TokenKind::Ampersand;
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(e))return pointerType(cast->typeName);
        if(auto name=std::dynamic_pointer_cast<NameExpr>(e))return rawPointers_.count(name->name)>0;
        return false;
    }

    void expression(const ExprPtr&e){
        if(!e)return;
        if(auto index=std::dynamic_pointer_cast<IndexExpr>(e)){
            expression(index->object);expression(index->index);
            if(auto name=std::dynamic_pointer_cast<NameExpr>(index->object)){
                auto it=bounds_.find(name->name);if(it!=bounds_.end()){index->hasFixedBound=true;index->fixedBound=it->second;}
                if(rawPointers_.count(name->name)>0&&!index->baseIsSlice)requireUnsafe(index->span,"raw pointer indexing");
            }else if(auto array=std::dynamic_pointer_cast<ArrayExpr>(index->object)){index->hasFixedBound=true;index->fixedBound=array->elements.size();}
            return;
        }
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(e)){
            expression(unary->operand);
            if(unary->op==TokenKind::Star)requireUnsafe(unary->span,"raw pointer dereference");
            return;
        }
        if(auto binary=std::dynamic_pointer_cast<BinaryExpr>(e)){expression(binary->left);expression(binary->right);return;}
        if(auto call=std::dynamic_pointer_cast<CallExpr>(e)){expression(call->callee);for(const auto&a:call->args)expression(a);return;}
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(e)){
            expression(cast->value);
            if(pointerType(cast->typeName))requireUnsafe(cast->span,"cast to raw pointer");
            return;
        }
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(e)){
            expression(member->object);
            if(member->baseIsPointer)requireUnsafe(member->span,"raw pointer member access");
            return;
        }
        if(auto array=std::dynamic_pointer_cast<ArrayExpr>(e)){for(const auto&item:array->elements)expression(item);return;}
    }

    void statement(const StmtPtr&s){
        if(!s)return;
        if(auto let=std::dynamic_pointer_cast<LetStmt>(s)){
            expression(let->initializer);
            if(let->annotation){
                if(auto count=fixedArrayCount(*let->annotation))bounds_[let->name]=*count;else bounds_.erase(let->name);
                if(pointerType(*let->annotation))rawPointers_.insert(let->name);else rawPointers_.erase(let->name);
            }else if(auto array=std::dynamic_pointer_cast<ArrayExpr>(let->initializer)){bounds_[let->name]=array->elements.size();rawPointers_.erase(let->name);}
            else{bounds_.erase(let->name);if(expressionProducesPointer(let->initializer))rawPointers_.insert(let->name);else rawPointers_.erase(let->name);}
            return;
        }
        if(auto expr=std::dynamic_pointer_cast<ExprStmt>(s)){expression(expr->expr);return;}
        if(auto ret=std::dynamic_pointer_cast<ReturnStmt>(s)){expression(ret->value);return;}
        if(auto thrown=std::dynamic_pointer_cast<ThrowStmt>(s)){expression(thrown->value);return;}
        if(auto block=std::dynamic_pointer_cast<BlockStmt>(s)){
            auto savedBounds=bounds_;auto savedPointers=rawPointers_;const auto savedUnsafe=unsafeDepth_;
            if(block->isUnsafe)++unsafeDepth_;
            for(const auto&child:block->statements)statement(child);
            bounds_=std::move(savedBounds);rawPointers_=std::move(savedPointers);unsafeDepth_=savedUnsafe;return;
        }
        if(auto branch=std::dynamic_pointer_cast<IfStmt>(s)){
            expression(branch->condition);auto savedBounds=bounds_;auto savedPointers=rawPointers_;
            statement(branch->thenBranch);bounds_=savedBounds;rawPointers_=savedPointers;
            statement(branch->elseBranch);bounds_=std::move(savedBounds);rawPointers_=std::move(savedPointers);return;
        }
        if(auto loop=std::dynamic_pointer_cast<WhileStmt>(s)){
            expression(loop->condition);auto savedBounds=bounds_;auto savedPointers=rawPointers_;
            statement(loop->body);bounds_=std::move(savedBounds);rawPointers_=std::move(savedPointers);return;
        }
    }

    Diagnostics&diagnostics_;
    std::unordered_map<std::string,std::size_t> bounds_;
    std::unordered_set<std::string> rawPointers_;
    std::size_t unsafeDepth_=0;
    bool allowUnsafe_=false;
};

} // namespace

bool SafetyAnnotator::check(const Program&program,Diagnostics&diagnostics,bool allowUnsafe)const{
    const auto before=diagnostics.items().size();
    for(const auto&statement:program.statements)if(auto fn=std::dynamic_pointer_cast<FunctionStmt>(statement);fn&&!fn->isExtern&&!fn->isForward){Annotator annotator(diagnostics,allowUnsafe);annotator.function(fn);}
    return diagnostics.items().size()==before;
}

} // namespace noe
