#include "noe.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noe {
namespace {

std::string calleeName(const ExprPtr& expression){if(auto name=std::dynamic_pointer_cast<NameExpr>(expression))return name->name;if(auto member=std::dynamic_pointer_cast<MemberExpr>(expression))if(auto base=std::dynamic_pointer_cast<NameExpr>(member->object))return base->name+"."+member->member;return{};}
std::optional<std::int64_t> literalInteger(const ExprPtr&expression){if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(expression))if(auto value=std::get_if<std::int64_t>(&literal->value))return*value;if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expression)){auto value=literalInteger(unary->operand);if(!value)return std::nullopt;if(unary->op==TokenKind::Plus)return*value;if(unary->op==TokenKind::Minus)return-*value;}return std::nullopt;}
bool nullExpression(const ExprPtr&expression){if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(expression))return std::holds_alternative<std::monostate>(literal->value);if(auto cast=std::dynamic_pointer_cast<CastExpr>(expression))return nullExpression(cast->value);return false;}
std::optional<std::size_t> fixedArrayCount(const std::optional<std::string>&annotation){if(!annotation||annotation->size()<4||annotation->front()!='['||annotation->rfind("[]",0)==0)return std::nullopt;auto semi=annotation->rfind(';');auto close=annotation->rfind(']');if(semi==std::string::npos||close==std::string::npos||semi>=close)return std::nullopt;try{return static_cast<std::size_t>(std::stoull(annotation->substr(semi+1,close-semi-1),nullptr,0));}catch(...){return std::nullopt;}}
bool borrowType(const std::optional<std::string>&annotation){return annotation&&(annotation->rfind("*",0)==0||annotation->rfind("[]",0)==0);}

struct FunctionSummary{
    std::unordered_set<std::size_t> returnParams;
    std::unordered_set<std::size_t> escapeParams;
    bool operator==(const FunctionSummary&o)const{return returnParams==o.returnParams&&escapeParams==o.escapeParams;}
};
using SummaryMap=std::unordered_map<std::string,FunctionSummary>;
using Origins=std::unordered_set<int>; // -1 means function-local; >=0 means parameter index.

void mergeOrigins(Origins&into,const Origins&from){for(int value:from)into.insert(value);}

class SummaryCollector{
public:
    SummaryCollector(const std::shared_ptr<FunctionStmt>&function,const SummaryMap&known):function_(function),known_(known){for(std::size_t i=0;i<function_->params.size();++i){paramIndex_[function_->params[i].name]=i;if(borrowType(function_->params[i].annotation))aliases_[function_->params[i].name].insert(static_cast<int>(i));}}
    FunctionSummary run(){if(function_->body)for(const auto&s:function_->body->statements)statement(s);return summary_;}
private:
    Origins sourceOrigins(const ExprPtr&e){
        if(!e)return{};
        if(auto n=std::dynamic_pointer_cast<NameExpr>(e)){auto p=paramIndex_.find(n->name);if(p!=paramIndex_.end())return Origins{static_cast<int>(p->second)};return Origins{-1};}
        if(auto index=std::dynamic_pointer_cast<IndexExpr>(e))return sourceOrigins(index->object);
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(e))return sourceOrigins(member->object);
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(e))return sourceOrigins(unary->operand);
        return Origins{-1};
    }
    Origins borrowOrigins(const ExprPtr&e){
        if(!e)return{};
        if(auto n=std::dynamic_pointer_cast<NameExpr>(e)){auto it=aliases_.find(n->name);if(it!=aliases_.end())return it->second;return{};}
        if(auto u=std::dynamic_pointer_cast<UnaryExpr>(e);u&&u->op==TokenKind::Ampersand)return sourceOrigins(u->operand);
        if(auto c=std::dynamic_pointer_cast<CastExpr>(e))return borrowOrigins(c->value);
        if(auto m=std::dynamic_pointer_cast<MemberExpr>(e)){auto base=std::dynamic_pointer_cast<NameExpr>(m->object);if(base){auto it=aggregate_.find(base->name+"."+m->member);if(it!=aggregate_.end())return it->second;}return{};}
        if(auto call=std::dynamic_pointer_cast<CallExpr>(e)){
            const auto name=calleeName(call->callee);
            if(name=="slice"&&!call->args.empty())return sourceOrigins(call->args.front());
            Origins result;auto it=known_.find(name);if(it==known_.end())return result;
            for(auto index:it->second.returnParams)if(index<call->args.size()){auto arg=borrowOrigins(call->args[index]);if(arg.empty())arg=sourceOrigins(call->args[index]);mergeOrigins(result,arg);}
            return result;
        }
        return{};
    }
    void observeCall(const std::shared_ptr<CallExpr>&call){
        auto it=known_.find(calleeName(call->callee));if(it==known_.end())return;
        for(auto index:it->second.escapeParams)if(index<call->args.size()){auto origins=borrowOrigins(call->args[index]);if(origins.empty())origins=sourceOrigins(call->args[index]);for(int origin:origins)if(origin>=0)summary_.escapeParams.insert(static_cast<std::size_t>(origin));}
    }
    void expression(const ExprPtr&e){if(!e)return;if(auto call=std::dynamic_pointer_cast<CallExpr>(e)){observeCall(call);for(const auto&a:call->args)expression(a);return;}if(auto u=std::dynamic_pointer_cast<UnaryExpr>(e)){expression(u->operand);return;}if(auto b=std::dynamic_pointer_cast<BinaryExpr>(e)){expression(b->left);expression(b->right);return;}if(auto c=std::dynamic_pointer_cast<CastExpr>(e)){expression(c->value);return;}if(auto i=std::dynamic_pointer_cast<IndexExpr>(e)){expression(i->object);expression(i->index);return;}if(auto m=std::dynamic_pointer_cast<MemberExpr>(e)){expression(m->object);return;}if(auto a=std::dynamic_pointer_cast<ArrayExpr>(e)){for(const auto&x:a->elements)expression(x);}}
    void assign(const ExprPtr&target,const ExprPtr&value){
        auto origins=borrowOrigins(value);
        if(auto n=std::dynamic_pointer_cast<NameExpr>(target)){if(origins.empty())aliases_.erase(n->name);else aliases_[n->name]=std::move(origins);return;}
        if(auto m=std::dynamic_pointer_cast<MemberExpr>(target)){if(auto base=std::dynamic_pointer_cast<NameExpr>(m->object)){const auto key=base->name+"."+m->member;if(origins.empty())aggregate_.erase(key);else aggregate_[key]=origins;for(int origin:origins)if(origin>=0)summary_.escapeParams.insert(static_cast<std::size_t>(origin));}return;}
        if(std::dynamic_pointer_cast<IndexExpr>(target)){for(int origin:origins)if(origin>=0)summary_.escapeParams.insert(static_cast<std::size_t>(origin));}
    }
    void statement(const StmtPtr&s){
        if(!s)return;
        if(auto let=std::dynamic_pointer_cast<LetStmt>(s)){expression(let->initializer);auto origins=borrowOrigins(let->initializer);if(origins.empty())aliases_.erase(let->name);else aliases_[let->name]=std::move(origins);return;}
        if(auto expr=std::dynamic_pointer_cast<ExprStmt>(s)){if(auto assignment=std::dynamic_pointer_cast<BinaryExpr>(expr->expr);assignment&&assignment->op==TokenKind::Equal){expression(assignment->right);assign(assignment->left,assignment->right);}else expression(expr->expr);return;}
        if(auto ret=std::dynamic_pointer_cast<ReturnStmt>(s)){expression(ret->value);auto origins=borrowOrigins(ret->value);if(auto n=std::dynamic_pointer_cast<NameExpr>(ret->value)){const std::string prefix=n->name+".";for(const auto&[key,fieldOrigins]:aggregate_)if(key.rfind(prefix,0)==0)mergeOrigins(origins,fieldOrigins);}for(int origin:origins)if(origin>=0)summary_.returnParams.insert(static_cast<std::size_t>(origin));return;}
        if(auto thrown=std::dynamic_pointer_cast<ThrowStmt>(s)){expression(thrown->value);return;}
        if(auto block=std::dynamic_pointer_cast<BlockStmt>(s)){for(const auto&child:block->statements)statement(child);return;}
        if(auto branch=std::dynamic_pointer_cast<IfStmt>(s)){expression(branch->condition);forEachBranch(branch->thenBranch);forEachBranch(branch->elseBranch);return;}
        if(auto loop=std::dynamic_pointer_cast<WhileStmt>(s)){expression(loop->condition);forEachBranch(loop->body);return;}
    }
    void forEachBranch(const StmtPtr&s){if(auto b=std::dynamic_pointer_cast<BlockStmt>(s)){for(const auto&child:b->statements)statement(child);}else statement(s);}
    std::shared_ptr<FunctionStmt> function_;const SummaryMap&known_;FunctionSummary summary_;std::unordered_map<std::string,std::size_t>paramIndex_;std::unordered_map<std::string,Origins>aliases_;std::unordered_map<std::string,Origins>aggregate_;
};

SummaryMap buildSummaries(const Program&program){
    std::vector<std::shared_ptr<FunctionStmt>>functions;SummaryMap summaries;
    for(const auto&s:program.statements)if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s);f&&!f->isExtern){functions.push_back(f);summaries[f->name]={};}
    const std::size_t limit=functions.size()*2+4;
    for(std::size_t pass=0;pass<limit;++pass){bool changed=false;for(const auto&f:functions){auto next=SummaryCollector(f,summaries).run();if(!(next==summaries[f->name])){summaries[f->name]=std::move(next);changed=true;}}if(!changed)break;}
    return summaries;
}

struct BorrowInfo{int sourceDepth=0;std::string source;bool parameter=false;std::size_t parameterIndex=0;};

class LifetimeInspector {
public:
    LifetimeInspector(Diagnostics&diagnostics,bool returnsBorrowable,const SummaryMap&summaries):diagnostics_(diagnostics),returnsBorrowable_(returnsBorrowable),summaries_(summaries){}
    void parameter(const Parameter&parameter,std::size_t index){declaredDepth_[parameter.name]=0;parameters_[parameter.name]=index;if(auto count=fixedArrayCount(parameter.annotation))arraySizes_[parameter.name]=*count;if(borrowType(parameter.annotation))borrowed_[parameter.name]=BorrowInfo{0,parameter.name,true,index};}
    void run(const std::shared_ptr<BlockStmt>&body){if(body)for(const auto&stmt:body->statements)statement(stmt,0);}
private:
    std::optional<BorrowInfo> sourceOf(const ExprPtr&expression){
        if(!expression)return std::nullopt;
        if(auto name=std::dynamic_pointer_cast<NameExpr>(expression)){auto depth=declaredDepth_.find(name->name);if(depth==declaredDepth_.end())return std::nullopt;auto p=parameters_.find(name->name);return BorrowInfo{depth->second,name->name,p!=parameters_.end(),p==parameters_.end()?0:p->second};}
        if(auto index=std::dynamic_pointer_cast<IndexExpr>(expression))return sourceOf(index->object);
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(expression))return sourceOf(member->object);
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expression))return sourceOf(unary->operand);
        return std::nullopt;
    }
    std::optional<BorrowInfo> borrowOf(const ExprPtr&expression){
        if(!expression)return std::nullopt;
        if(auto name=std::dynamic_pointer_cast<NameExpr>(expression)){auto it=borrowed_.find(name->name);if(it!=borrowed_.end())return it->second;return std::nullopt;}
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expression);unary&&unary->op==TokenKind::Ampersand){auto source=sourceOf(unary->operand);if(source){if(auto n=std::dynamic_pointer_cast<NameExpr>(unary->operand);n&&parameters_.count(n->name)){source->parameter=false;source->source="address of parameter variable '"+n->name+"'";}return source;}}
        if(auto call=std::dynamic_pointer_cast<CallExpr>(expression)){
            const auto name=calleeName(call->callee);if(name=="slice"&&!call->args.empty())return sourceOf(call->args.front());
            auto it=summaries_.find(name);if(it!=summaries_.end())for(auto index:it->second.returnParams)if(index<call->args.size()){auto source=borrowOf(call->args[index]);if(!source)source=sourceOf(call->args[index]);if(source)return source;}
        }
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(expression)){if(auto base=std::dynamic_pointer_cast<NameExpr>(member->object)){auto it=aggregateBorrows_.find(base->name+"."+member->member);if(it!=aggregateBorrows_.end())return it->second;}}
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(expression))return borrowOf(cast->value);
        return std::nullopt;
    }
    void checkIndex(const std::shared_ptr<IndexExpr>&index){
        // The original literal may have begun as [int; N], but the type
        // checker promoted this local to a growable owned list. Its initial
        // element count is not a permanent bounds limit.
        if(index->growableListRead)return;
        auto literal=literalInteger(index->index);if(!literal)return;std::optional<std::size_t> count;if(auto name=std::dynamic_pointer_cast<NameExpr>(index->object)){auto it=arraySizes_.find(name->name);if(it!=arraySizes_.end())count=it->second;}else if(auto array=std::dynamic_pointer_cast<ArrayExpr>(index->object))count=array->elements.size();if(count&&(*literal<0||static_cast<std::uint64_t>(*literal)>=static_cast<std::uint64_t>(*count)))diagnostics_.error("NQR-L4104",index->span,"fixed-array index is out of bounds","ordinary fixed-array accesses are checked statically when the index is known");}
    void checkEscapingCall(const std::shared_ptr<CallExpr>&call){auto it=summaries_.find(calleeName(call->callee));if(it==summaries_.end())return;for(auto index:it->second.escapeParams)if(index<call->args.size()){auto source=borrowOf(call->args[index]);if(!source)source=sourceOf(call->args[index]);if(source&&!source->parameter)diagnostics_.error("NQR-L4106",call->span,"call may store a borrow of local storage ('"+source->source+"') beyond its safe lifetime","pass caller-owned storage or copy owned data instead");}}
    void expression(const ExprPtr&expr){
        if(!expr)return;
        if(auto name=std::dynamic_pointer_cast<NameExpr>(expr)){if(dangling_.count(name->name))diagnostics_.error("NQR-L4102",name->span,"use of reference whose borrowed storage has left scope","rebind the reference to caller-owned or longer-lived storage before using it");return;}
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expr)){if(unary->op==TokenKind::Star&&nullExpression(unary->operand))diagnostics_.error("NQR-L4103",unary->span,"provable null pointer dereference","guard nullable pointers before dereferencing");expression(unary->operand);return;}
        if(auto index=std::dynamic_pointer_cast<IndexExpr>(expr)){if(nullExpression(index->object))diagnostics_.error("NQR-L4103",index->span,"provable null pointer indexing","guard nullable pointers before indexing");checkIndex(index);expression(index->object);expression(index->index);return;}
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(expr)){if(auto base=std::dynamic_pointer_cast<NameExpr>(member->object)){const auto key=base->name+"."+member->member;if(danglingFields_.count(key))diagnostics_.error("NQR-L4107",member->span,"aggregate field '"+key+"' contains a dangling borrow","replace the field before reading it or keep the borrowed storage alive");}expression(member->object);return;}
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(expr)){expression(cast->value);return;}
        if(auto call=std::dynamic_pointer_cast<CallExpr>(expr)){checkEscapingCall(call);expression(call->callee);for(const auto&arg:call->args)expression(arg);return;}
        if(auto binary=std::dynamic_pointer_cast<BinaryExpr>(expr)){expression(binary->left);expression(binary->right);return;}
        if(auto array=std::dynamic_pointer_cast<ArrayExpr>(expr)){for(const auto&item:array->elements)expression(item);return;}
    }
    void assignBorrow(const std::string&name,const ExprPtr&value){dangling_.erase(name);auto borrowed=borrowOf(value);if(borrowed)borrowed_[name]=*borrowed;else borrowed_.erase(name);}
    void assignAggregate(const std::shared_ptr<MemberExpr>&target,const ExprPtr&value){auto base=std::dynamic_pointer_cast<NameExpr>(target->object);if(!base)return;const auto key=base->name+"."+target->member;danglingFields_.erase(key);auto borrowed=borrowOf(value);if(borrowed)aggregateBorrows_[key]=*borrowed;else aggregateBorrows_.erase(key);}
    void expireScope(int depth){
        std::vector<std::string>locals;for(const auto&[name,declared]:declaredDepth_)if(declared==depth)locals.push_back(name);
        for(const auto&[name,borrow]:borrowed_){auto declared=declaredDepth_.find(name);if(!borrow.parameter&&borrow.sourceDepth>=depth&&declared!=declaredDepth_.end()&&declared->second<depth)dangling_.insert(name);}
        for(const auto&[key,borrow]:aggregateBorrows_){auto dot=key.find('.');const auto base=key.substr(0,dot);auto declared=declaredDepth_.find(base);if(!borrow.parameter&&borrow.sourceDepth>=depth&&declared!=declaredDepth_.end()&&declared->second<depth)danglingFields_.insert(key);}
        for(const auto&name:locals){declaredDepth_.erase(name);arraySizes_.erase(name);borrowed_.erase(name);dangling_.erase(name);parameters_.erase(name);const std::string prefix=name+".";std::vector<std::string>fields;for(const auto&[key,_]:aggregateBorrows_)if(key.rfind(prefix,0)==0)fields.push_back(key);for(const auto&key:fields){aggregateBorrows_.erase(key);danglingFields_.erase(key);}}
    }
    void checkReturn(const std::shared_ptr<ReturnStmt>&returned){
        expression(returned->value);if(!returnsBorrowable_)return;
        auto borrowed=borrowOf(returned->value);if(borrowed&&!borrowed->parameter)diagnostics_.error("NQR-L4101",returned->span,"cannot return a reference borrowed from function-local storage ('"+borrowed->source+"')","return owned data or a borrow derived from caller-owned storage instead");
        if(auto name=std::dynamic_pointer_cast<NameExpr>(returned->value)){const std::string prefix=name->name+".";for(const auto&[key,fieldBorrow]:aggregateBorrows_)if(key.rfind(prefix,0)==0&&!fieldBorrow.parameter)diagnostics_.error("NQR-L4108",returned->span,"returned aggregate contains a borrow of function-local storage ('"+fieldBorrow.source+"')","copy owned data into the aggregate or borrow caller-owned storage");}
    }
    void statement(const StmtPtr&stmt,int depth){
        if(!stmt)return;
        if(auto let=std::dynamic_pointer_cast<LetStmt>(stmt)){expression(let->initializer);declaredDepth_[let->name]=depth;if(auto count=fixedArrayCount(let->annotation))arraySizes_[let->name]=*count;else if(auto array=std::dynamic_pointer_cast<ArrayExpr>(let->initializer))arraySizes_[let->name]=array->elements.size();assignBorrow(let->name,let->initializer);return;}
        if(auto expr=std::dynamic_pointer_cast<ExprStmt>(stmt)){if(auto assignment=std::dynamic_pointer_cast<BinaryExpr>(expr->expr);assignment&&assignment->op==TokenKind::Equal){expression(assignment->right);if(auto target=std::dynamic_pointer_cast<NameExpr>(assignment->left)){assignBorrow(target->name,assignment->right);return;}if(auto member=std::dynamic_pointer_cast<MemberExpr>(assignment->left)){expression(member->object);assignAggregate(member,assignment->right);return;}}expression(expr->expr);return;}
        if(auto returned=std::dynamic_pointer_cast<ReturnStmt>(stmt)){checkReturn(returned);return;}
        if(auto thrown=std::dynamic_pointer_cast<ThrowStmt>(stmt)){expression(thrown->value);return;}
        if(auto block=std::dynamic_pointer_cast<BlockStmt>(stmt)){for(const auto&child:block->statements)statement(child,depth+1);expireScope(depth+1);return;}
        if(auto branch=std::dynamic_pointer_cast<IfStmt>(stmt)){expression(branch->condition);statement(branch->thenBranch,depth+1);expireScope(depth+1);statement(branch->elseBranch,depth+1);expireScope(depth+1);return;}
        if(auto loop=std::dynamic_pointer_cast<WhileStmt>(stmt)){expression(loop->condition);statement(loop->body,depth+1);expireScope(depth+1);return;}
    }
    Diagnostics&diagnostics_;bool returnsBorrowable_=false;const SummaryMap&summaries_;std::unordered_map<std::string,int>declaredDepth_;std::unordered_map<std::string,std::size_t>arraySizes_;std::unordered_map<std::string,std::size_t>parameters_;std::unordered_map<std::string,BorrowInfo>borrowed_;std::unordered_map<std::string,BorrowInfo>aggregateBorrows_;std::unordered_set<std::string>dangling_;std::unordered_set<std::string>danglingFields_;
};

} // namespace

bool BorrowChecker::check(const Program&program,Diagnostics&diagnostics)const{
    const auto summaries=buildSummaries(program);
    for(const auto&statement:program.statements){auto function=std::dynamic_pointer_cast<FunctionStmt>(statement);if(!function||function->isExtern||!function->body)continue;const bool returnsBorrowable=function->returnType&&(function->returnType->rfind("[]",0)==0||function->returnType->rfind("*",0)==0);LifetimeInspector inspector(diagnostics,returnsBorrowable,summaries);for(std::size_t i=0;i<function->params.size();++i)inspector.parameter(function->params[i],i);inspector.run(function->body);}
    return!diagnostics.hasErrors();
}

} // namespace noe
