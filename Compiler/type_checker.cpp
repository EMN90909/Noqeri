#include "noe.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

namespace noe {
namespace {
Type simple(TypeKind kind){Type t;t.kind=kind;return t;}
std::string calleeName(const ExprPtr& expr){
    if(auto n=std::dynamic_pointer_cast<NameExpr>(expr))return n->name;
    if(auto m=std::dynamic_pointer_cast<MemberExpr>(expr)){
        if(auto n=std::dynamic_pointer_cast<NameExpr>(m->object))return n->name+"."+m->member;
    }
    return {};
}
std::optional<std::string> literalString(const ExprPtr& expr){
    auto l=std::dynamic_pointer_cast<LiteralExpr>(expr);
    if(!l)return std::nullopt;
    if(auto s=std::get_if<std::string>(&l->value))return *s;
    return std::nullopt;
}
std::optional<std::int64_t> literalInteger(const ExprPtr& expr){
    if(auto l=std::dynamic_pointer_cast<LiteralExpr>(expr)){
        if(auto n=std::get_if<std::int64_t>(&l->value))return *n;
    }
    if(auto u=std::dynamic_pointer_cast<UnaryExpr>(expr)){
        auto value=literalInteger(u->operand);
        if(!value)return std::nullopt;
        if(u->op==TokenKind::Plus)return *value;
        if(u->op==TokenKind::Minus){
            if(*value==std::numeric_limits<std::int64_t>::min())return std::nullopt;
            return -*value;
        }
    }
    return std::nullopt;
}
bool literalFitsInteger(const Type& target,std::int64_t value){
    switch(target.kind){
        case TypeKind::U8:return value>=0&&value<=std::numeric_limits<std::uint8_t>::max();
        case TypeKind::U16:return value>=0&&value<=std::numeric_limits<std::uint16_t>::max();
        case TypeKind::U32:return value>=0&&static_cast<std::uint64_t>(value)<=std::numeric_limits<std::uint32_t>::max();
        case TypeKind::U64:case TypeKind::Usize:return value>=0;
        case TypeKind::I8:return value>=std::numeric_limits<std::int8_t>::min()&&value<=std::numeric_limits<std::int8_t>::max();
        case TypeKind::I16:return value>=std::numeric_limits<std::int16_t>::min()&&value<=std::numeric_limits<std::int16_t>::max();
        case TypeKind::I32:return value>=std::numeric_limits<std::int32_t>::min()&&value<=std::numeric_limits<std::int32_t>::max();
        case TypeKind::I64:case TypeKind::Isize:case TypeKind::Int:return true;
        default:return false;
    }
}
bool canAssignValue(const Type& target,const Type& value,const ExprPtr& expr){
    if(canAssign(target,value))return true;
    if(target.isInteger()&&value.isInteger()){
        if(auto literal=literalInteger(expr))return literalFitsInteger(target,*literal);
    }
    return false;
}
}


// Deliberately bounded callsite inference: direct scalar literals only.
// No guessed dataflow, inferred recursion, promoted mixed numeric types,
// dynamic typing, or hidden runtime specialization.
static std::optional<Type> privateLiteralParameterType(const ExprPtr& expression) {
    if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(expression)){
        if(std::holds_alternative<std::int64_t>(literal->value))return simple(TypeKind::Int);
        if(std::holds_alternative<double>(literal->value))return simple(TypeKind::Float);
        if(std::holds_alternative<bool>(literal->value))return simple(TypeKind::Bool);
        if(std::holds_alternative<std::string>(literal->value))return simple(TypeKind::String);
    }
    if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expression)){
        if(unary->op==TokenKind::Minus||unary->op==TokenKind::Plus){
            auto value=privateLiteralParameterType(unary->operand);
            if(value&&value->isNumeric())return value;
        }
    }
    return std::nullopt;
}

TypeChecker::TypeChecker(Diagnostics& diagnostics):diagnostics_(diagnostics){pushScope();currentReturn_=simple(TypeKind::Void);}
void TypeChecker::pushScope(){scopes_.push_back({});constScopes_.push_back({});}
void TypeChecker::popScope(){scopes_.pop_back();constScopes_.pop_back();}
void TypeChecker::define(const std::string&name,Type type,bool isConst,Span span){auto&scope=scopes_.back();auto&cs=constScopes_.back();if(scope.count(name))diagnostics_.error("NOE-T3003",span,"symbol '"+name+"' is already defined in this scope");else{scope[name]=std::move(type);cs[name]=isConst;}}
std::optional<Type> TypeChecker::resolve(const std::string&name)const{for(auto it=scopes_.rbegin();it!=scopes_.rend();++it){auto f=it->find(name);if(f!=it->end())return f->second;}return std::nullopt;}
bool TypeChecker::isConstSymbol(const std::string&name)const{for(auto it=constScopes_.rbegin();it!=constScopes_.rend();++it){auto f=it->find(name);if(f!=it->end())return f->second;}return false;}

Type TypeChecker::resolveType(const std::string& name,Span span){
    if(activeGenericParams_.count(name)){Type t;t.kind=TypeKind::Generic;t.genericName=name;return t;}
    if(name.rfind("*",0)==0){std::string rest=name.substr(1);bool vol=false;if(rest.rfind("volatile ",0)==0){vol=true;rest=rest.substr(9);}Type t;t.kind=TypeKind::Pointer;t.isVolatile=vol;t.pointee=std::make_shared<Type>(resolveType(rest,span));return t;}
    if(name.rfind("[]",0)==0){Type t;t.kind=TypeKind::Slice;t.element=std::make_shared<Type>(resolveType(name.substr(2),span));return t;}
    if(name.size()>3&&name.front()=='['&&name.back()==']'){
        auto semi=name.rfind(';');
        if(semi!=std::string::npos){Type t;t.kind=TypeKind::Array;t.element=std::make_shared<Type>(resolveType(name.substr(1,semi-1),span));try{t.count=static_cast<std::size_t>(std::stoull(name.substr(semi+1,name.size()-semi-2),nullptr,0));}catch(...){t.count=0;}return t;}
    }
    Type t=typeFromName(name);
    if(t.kind==TypeKind::Record){
        auto record=records_.find(t.recordName);
        if(record==records_.end())diagnostics_.error("NOE-T3000",span,"unknown type '"+name+"'");
        else{t.recordSize=record->second.size;t.recordAlignment=record->second.alignment;}
    }
    return t;
}

std::optional<RecordField> TypeChecker::resolveField(const Type&base,const std::string&member)const{
    Type t=base;if(t.kind==TypeKind::Pointer&&t.pointee)t=*t.pointee;if(t.kind!=TypeKind::Record)return std::nullopt;
    auto r=records_.find(t.recordName);if(r==records_.end())return std::nullopt;for(const auto&f:r->second.fields)if(f.name==member)return f;return std::nullopt;
}

bool TypeChecker::bindGeneric(const Type& pattern,const Type& actual,std::unordered_map<std::string,Type>& bindings) const{
    if(pattern.kind==TypeKind::Generic){auto it=bindings.find(pattern.genericName);if(it==bindings.end()){bindings[pattern.genericName]=actual;return true;}return canAssign(it->second,actual)&&canAssign(actual,it->second);}
    if(pattern.kind!=actual.kind){if(pattern.kind==TypeKind::Unknown||actual.kind==TypeKind::Unknown)return true;return false;}
    if(pattern.kind==TypeKind::Pointer&&pattern.pointee&&actual.pointee)return bindGeneric(*pattern.pointee,*actual.pointee,bindings);
    if((pattern.kind==TypeKind::Slice||pattern.kind==TypeKind::Array)&&pattern.element&&actual.element){if(pattern.kind==TypeKind::Array&&pattern.count!=actual.count)return false;return bindGeneric(*pattern.element,*actual.element,bindings);}
    return canAssign(pattern,actual);
}
Type TypeChecker::substituteGeneric(const Type& type,const std::unordered_map<std::string,Type>& bindings) const{
    if(type.kind==TypeKind::Generic){auto it=bindings.find(type.genericName);return it==bindings.end()?type:it->second;}
    Type out=type;
    if(type.pointee)out.pointee=std::make_shared<Type>(substituteGeneric(*type.pointee,bindings));
    if(type.element)out.element=std::make_shared<Type>(substituteGeneric(*type.element,bindings));
    return out;
}

bool TypeChecker::check(const Program&program){
    functions_.clear();records_.clear();
    functions_["print"]={std::vector<Type>{simple(TypeKind::Unknown)},simple(TypeKind::Void),{}};
    functions_["clockMillis"]={{},simple(TypeKind::I64),{}};
    functions_["platform"]={{},simple(TypeKind::String),{}};
    functions_["textLength"]={std::vector<Type>{simple(TypeKind::String)},simple(TypeKind::Usize),{}};

    for(const auto&stmt:program.statements)if(auto r=std::dynamic_pointer_cast<RecordStmt>(stmt))records_[r->name]=RecordType{};
    for(const auto&stmt:program.statements)if(auto r=std::dynamic_pointer_cast<RecordStmt>(stmt)){
        std::size_t offset=0,maxAlign=1;auto&rt=records_[r->name];
        for(auto&field:r->fields){Type ft=resolveType(field.typeName,field.span);std::size_t size=ft.size(),align=ft.alignment();if(ft.kind==TypeKind::Record){auto nested=records_.find(ft.recordName);if(nested!=records_.end()){size=nested->second.size;align=nested->second.alignment;}}if(size==0){diagnostics_.error("NOE-T3020",field.span,"record field '"+field.name+"' has incomplete or zero-sized type '"+field.typeName+"'","use a pointer for recursive/forward record references");size=1;}offset=(offset+align-1)/align*align;field.offset=offset;field.size=size;offset+=size;maxAlign=std::max(maxAlign,align);rt.fields.push_back(field);}rt.alignment=maxAlign;rt.size=(offset+maxAlign-1)/maxAlign*maxAlign;r->size=rt.size;r->alignment=rt.alignment;
    }

    for(const auto&stmt:program.statements)if(auto fn=std::dynamic_pointer_cast<FunctionStmt>(stmt)){
        activeGenericParams_.clear();for(const auto&g:fn->genericParams)activeGenericParams_.insert(g);
        std::vector<Type>params;for(const auto&p:fn->params)params.push_back(p.annotation?resolveType(*p.annotation,p.span):simple(TypeKind::Unknown));
        // Unknown is a temporary placeholder for a private inferred return.
        // Functions without return values resolve to void in checkStmt.
        Type ret=fn->returnType?resolveType(*fn->returnType,fn->span):
            (fn->isExport||fn->isExtern||fn->isForward||!fn->genericParams.empty()?
             simple(TypeKind::Void):simple(TypeKind::Unknown));
        FunctionType next{params,ret,fn->genericParams,fn->isExtern,fn->isExport,{},fn->isForward};
        auto existing=functions_.find(fn->name);
        if(existing==functions_.end())functions_[fn->name]=next;
        else{
            const bool sameSignature=existing->second.params==next.params&&existing->second.result==next.result&&existing->second.genericParams==next.genericParams;
            if(!sameSignature)diagnostics_.error("NOE-T3021",fn->span,"function '"+fn->name+"' conflicts with an earlier declaration");
            else if(existing->second.isForward&&!next.isForward)functions_[fn->name]=next;
            else if(!existing->second.isForward&&next.isForward){}
            else if(existing->second.isForward&&next.isForward){}
            else diagnostics_.error("NOE-T3021",fn->span,"function '"+fn->name+"' is already declared");
        }
        activeGenericParams_.clear();
    }

    // Collect all callsites before checking bodies. Missing annotations on
    // private functions are accepted only with consistent literal evidence.
    std::vector<std::shared_ptr<CallExpr>> calls;
    std::function<void(const ExprPtr&)> gatherExpr;
    std::function<void(const StmtPtr&)> gatherStmt;
    gatherExpr=[&](const ExprPtr& expr) {
        if(!expr)return;
        if(auto call=std::dynamic_pointer_cast<CallExpr>(expr)){
            calls.push_back(call);gatherExpr(call->callee);
            for(const auto& arg:call->args)gatherExpr(arg);
        } else if(auto binary=std::dynamic_pointer_cast<BinaryExpr>(expr)){
            gatherExpr(binary->left);gatherExpr(binary->right);
        } else if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expr)){
            gatherExpr(unary->operand);
        } else if(auto cast=std::dynamic_pointer_cast<CastExpr>(expr)){
            gatherExpr(cast->value);
        } else if(auto index=std::dynamic_pointer_cast<IndexExpr>(expr)){
            gatherExpr(index->object);gatherExpr(index->index);
        } else if(auto member=std::dynamic_pointer_cast<MemberExpr>(expr)){
            gatherExpr(member->object);
        } else if(auto array=std::dynamic_pointer_cast<ArrayExpr>(expr)){
            for(const auto& element:array->elements)gatherExpr(element);
        }
    };
    gatherStmt=[&](const StmtPtr& stmt){
        if(!stmt)return;
        if(auto block=std::dynamic_pointer_cast<BlockStmt>(stmt)){
            for(const auto& item:block->statements)gatherStmt(item);
        }else if(auto fn=std::dynamic_pointer_cast<FunctionStmt>(stmt)){
            gatherStmt(fn->body);
        }else if(auto binding=std::dynamic_pointer_cast<LetStmt>(stmt)){
            gatherExpr(binding->initializer);
        }else if(auto expression=std::dynamic_pointer_cast<ExprStmt>(stmt)){
            gatherExpr(expression->expr);
        }else if(auto ret=std::dynamic_pointer_cast<ReturnStmt>(stmt)){
            gatherExpr(ret->value);
        }else if(auto thrown=std::dynamic_pointer_cast<ThrowStmt>(stmt)){
            gatherExpr(thrown->value);
        }else if(auto branch=std::dynamic_pointer_cast<IfStmt>(stmt)){
            gatherExpr(branch->condition);gatherStmt(branch->thenBranch);gatherStmt(branch->elseBranch);
        }else if(auto loop=std::dynamic_pointer_cast<WhileStmt>(stmt)){
            gatherExpr(loop->condition);gatherStmt(loop->body);
        }
    };
    for(const auto& stmt:program.statements)gatherStmt(stmt);
    for(const auto& stmt:program.statements){
        auto fn=std::dynamic_pointer_cast<FunctionStmt>(stmt);
        if(!fn)continue;
        auto existing=functions_.find(fn->name);
        if(existing==functions_.end())continue;
        for(std::size_t i=0;i<fn->params.size();++i){
            if(fn->params[i].annotation)continue;
            auto& type=existing->second.params[i];
            if(fn->isExport||fn->isExtern||fn->isForward||!fn->genericParams.empty()){
                diagnostics_.error("NQR-T3063",fn->params[i].span,
                    "public, external, generic or forward parameter '"+fn->params[i].name+"' needs an explicit type",
                    "write '"+fn->params[i].name+": Type' in the declaration");
                continue;
            }
            std::optional<Type> inferred;
            bool conflict=false;
            for(const auto& call:calls){
                auto name=std::dynamic_pointer_cast<NameExpr>(call->callee);
                if(!name||name->name!=fn->name||i>=call->args.size())continue;
                auto evidence=privateLiteralParameterType(call->args[i]);
                if(!evidence)continue;
                if(inferred&&*inferred!=*evidence){conflict=true;break;}
                inferred=evidence;
            }
            if(conflict){
                diagnostics_.error("NQR-T3064",fn->params[i].span,
                    "conflicting literal call-site types for private parameter '"+fn->params[i].name+"'",
                    "add an explicit ': Type' annotation and cast incompatible arguments");
            }else if(!inferred){
                diagnostics_.error("NQR-T3065",fn->params[i].span,
                    "cannot infer private parameter '"+fn->params[i].name+"' from literal call sites",
                    "annotate this parameter explicitly, e.g. '"+fn->params[i].name+": int'");
            }else{
                type=*inferred;
                fn->params[i].annotation=type.name();
            }
        }
    }
    for(const auto&stmt:program.statements)checkStmt(stmt);
    return!diagnostics_.hasErrors();
}

void TypeChecker::checkStmt(const StmtPtr&stmt){
    if(std::dynamic_pointer_cast<RecordStmt>(stmt)||std::dynamic_pointer_cast<ImportStmt>(stmt)||std::dynamic_pointer_cast<ModuleStmt>(stmt))return;
    if(auto s=std::dynamic_pointer_cast<LetStmt>(stmt)){
        if(s->partition){
            if(!s->annotation||!s->zeroInitialized){
                diagnostics_.error("NQR-PGAS201",s->span,"partition declarations require uninitialized typed storage: let name: [i64; N]");
                return;
            }
            Type p=resolveType(*s->annotation,s->span);
            if(p.kind!=TypeKind::Array||!p.element||
               p.element->kind!=TypeKind::I64||p.count==0||
               p.count>1000000){
                diagnostics_.error("NQR-PGAS202",s->span,"PGAS partitions require fixed [i64; N] arrays with 1..1000000 elements");
                return;
            }
            s->partition->elements=p.count;
            define(s->name,p,false,s->span);
            return;
        }
        if(s->zeroInitialized){
            if(!s->annotation){diagnostics_.error("NOE-T3055",s->span,"zero-initialized local requires a type annotation");define(s->name,simple(TypeKind::Unknown),s->isConst,s->span);return;}
            Type declared=resolveType(*s->annotation,s->span);
            if(declared.kind==TypeKind::Void)diagnostics_.error("NOE-T3056",s->span,"void cannot be used as a local variable type");
            s->zeroRequiresStorage=declared.kind==TypeKind::Array||declared.kind==TypeKind::Record;
            s->zeroStorageBytes=s->zeroRequiresStorage?std::max<std::size_t>(1,declared.size()):0;
            define(s->name,declared,s->isConst,s->span);return;
        }
        if(s->annotation){
            Type declared=resolveType(*s->annotation,s->span);
            if(auto array=std::dynamic_pointer_cast<ArrayExpr>(s->initializer);array&&array->elements.empty()&&declared.kind==TypeKind::Array&&declared.element){
                if(declared.count!=0)diagnostics_.error("NOE-T3034",array->span,"empty array literal can initialize only a zero-length fixed array");
                array->elementSize=std::max<std::size_t>(1,declared.element->size());
                define(s->name,declared,s->isConst,s->span);return;
            }
            Type init=checkExpr(s->initializer);
            if(!canAssignValue(declared,init,s->initializer))diagnostics_.error("NOE-T3001",s->span,"cannot assign "+init.name()+" to "+declared.name(),"use an explicit 'as' cast for narrowing or signedness-changing integer conversions");
            define(s->name,declared,s->isConst,s->span);return;
        }
        Type init=checkExpr(s->initializer);define(s->name,init,s->isConst,s->span);return;
    }
    if(auto s=std::dynamic_pointer_cast<ExprStmt>(stmt)){checkExpr(s->expr);return;}
    if(auto s=std::dynamic_pointer_cast<BlockStmt>(stmt)){pushScope();for(const auto&x:s->statements)checkStmt(x);popScope();return;}
    if(auto s=std::dynamic_pointer_cast<IfStmt>(stmt)){Type c=checkExpr(s->condition);if(c.kind!=TypeKind::Bool&&c.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3004",s->condition->span,"if condition must be bool, found "+c.name());checkStmt(s->thenBranch);if(s->elseBranch)checkStmt(s->elseBranch);return;}
    if(auto s=std::dynamic_pointer_cast<WhileStmt>(stmt)){Type c=checkExpr(s->condition);if(c.kind!=TypeKind::Bool&&c.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3004",s->condition->span,"while condition must be bool, found "+c.name());++loopDepth_;checkStmt(s->body);--loopDepth_;return;}
    if(auto s=std::dynamic_pointer_cast<BreakStmt>(stmt)){if(loopDepth_==0)diagnostics_.error("NOE-T3057",s->span,"break is only valid inside a loop");return;}
    if(auto s=std::dynamic_pointer_cast<ContinueStmt>(stmt)){if(loopDepth_==0)diagnostics_.error("NOE-T3058",s->span,"continue is only valid inside a loop");return;}
    if(auto s=std::dynamic_pointer_cast<ReturnStmt>(stmt)){Type v=s->value?checkExpr(s->value):simple(TypeKind::Void);if(!canAssignValue(currentReturn_,v,s->value))diagnostics_.error("NOE-T3005",s->span,"return type mismatch: expected "+currentReturn_.name()+", found "+v.name());return;}
    if(auto s=std::dynamic_pointer_cast<ThrowStmt>(stmt)){Type code=checkExpr(s->value);if(!insideFunction_)diagnostics_.error("NOE-T3031",s->span,"throw is only valid inside a function");if(!code.isInteger()&&code.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3032",s->span,"throw requires an integer error code");if(!currentReturn_.isInteger()&&currentReturn_.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3033",s->span,"throw requires an integer-returning function","use an integer status return such as isize when using lightweight try/throw");return;}
    if(auto s=std::dynamic_pointer_cast<FunctionStmt>(stmt)){
        auto sig=functions_[s->name];if(s->isExtern||s->isForward)return;
        Type saved=currentReturn_;bool savedInside=insideFunction_;auto savedGenerics=activeGenericParams_;const auto savedLoopDepth=loopDepth_;
        currentReturn_=sig.result;insideFunction_=true;loopDepth_=0;activeGenericParams_.clear();for(const auto&g:s->genericParams)activeGenericParams_.insert(g);
        pushScope();for(std::size_t i=0;i<s->params.size();++i)define(s->params[i].name,sig.params[i],false,s->params[i].span);
        // Only a single unconditional return is inferred. This is deliberately
        // stricter than flow-sensitive inference; it avoids hidden recursive
        // type cycles, ambiguous branches and unstable exported signatures.
        if(!s->returnType) {
            auto only=s->body->statements.size()==1?
                std::dynamic_pointer_cast<ReturnStmt>(s->body->statements.front()):nullptr;
            if(only && only->value) {
                if(s->isExport || !s->genericParams.empty()) {
                    diagnostics_.error("NQR-T3061",s->span,
                        "public or generic function '"+s->name+"' needs an explicit return type",
                        "add ': Type' after the parameter list to document the API contract");
                } else if(std::any_of(s->params.begin(),s->params.end(),
                                  [](const Parameter& p){return !p.annotation;})) {
                    diagnostics_.error("NQR-T3060",s->span,
                        "cannot infer the return type of '"+s->name+"' with untyped parameters",
                        "annotate parameters, for example function add(a: int, b: int) = a + b");
                } else {
                    const Type inferred=checkExpr(only->value);
                    if(inferred.kind==TypeKind::Unknown || inferred.kind==TypeKind::Null ||
                       inferred.kind==TypeKind::Generic || inferred.kind==TypeKind::Void) {
                        diagnostics_.error("NQR-T3060",s->span,
                            "return type of '"+s->name+"' is not unambiguous",
                            "add ': Type' after the parameter list (required for recursion or unresolved calls)");
                    } else {
                        sig.result=inferred;
                        functions_[s->name].result=inferred;
                        s->returnType=inferred.name();
                    }
                }
            } else if(sig.result.kind==TypeKind::Unknown) {
                // The absence of a return expression means a void function.
                // More complex control flow with value returns must be typed.
                sig.result=simple(TypeKind::Void);
                functions_[s->name].result=sig.result;
            }
        }
        currentReturn_=sig.result;
        for(const auto&x:s->body->statements)checkStmt(x);
        popScope();
        currentReturn_=saved;insideFunction_=savedInside;loopDepth_=savedLoopDepth;activeGenericParams_=std::move(savedGenerics);return;
    }
}

Type TypeChecker::checkExpr(const ExprPtr&expr){
    if(auto e=std::dynamic_pointer_cast<LiteralExpr>(expr)){if(std::holds_alternative<std::monostate>(e->value))return simple(TypeKind::Null);if(std::holds_alternative<std::int64_t>(e->value))return simple(TypeKind::Int);if(std::holds_alternative<double>(e->value))return simple(TypeKind::Float);if(std::holds_alternative<bool>(e->value))return simple(TypeKind::Bool);return simple(TypeKind::String);}
    if(auto e=std::dynamic_pointer_cast<ArrayExpr>(expr)){
        if(e->elements.empty()){diagnostics_.error("NOE-T3034",e->span,"cannot infer the element type of an empty array literal","add a typed non-empty initializer");return simple(TypeKind::Unknown);}
        Type element=checkExpr(e->elements.front());for(std::size_t i=1;i<e->elements.size();++i){Type t=checkExpr(e->elements[i]);if(!canAssignValue(element,t,e->elements[i]))diagnostics_.error("NOE-T3035",e->elements[i]->span,"array literal elements must have one compatible type");}
        e->elementSize=std::max<std::size_t>(1,element.size());Type out;out.kind=TypeKind::Array;out.element=std::make_shared<Type>(element);out.count=e->elements.size();return out;
    }
    if(auto e=std::dynamic_pointer_cast<NameExpr>(expr)){auto t=resolve(e->name);if(t){e->aggregateStorage=t->kind==TypeKind::Array||t->kind==TypeKind::Record||t->kind==TypeKind::Slice;return*t;}if(functions_.count(e->name)||e->name=="host"||e->name=="abi")return simple(TypeKind::Unknown);diagnostics_.error("NOE-T3002",e->span,"unknown symbol '"+e->name+"'");return simple(TypeKind::Unknown);}
    if(auto e=std::dynamic_pointer_cast<UnaryExpr>(expr)){
        Type t=checkExpr(e->operand);
        if(e->op==TokenKind::Question&&t.isInteger())e->op=TokenKind::Try;
        if(e->op==TokenKind::Question){
            const bool sameReturn=insideFunction_&&currentReturn_==t;
            const bool recordPointer=t.kind==TypeKind::Pointer&&t.pointee&&
                t.pointee->kind==TypeKind::Record;
            const bool isResult=recordPointer&&(
                t.pointee->recordName.rfind("Result__nqg__",0)==0||
                t.pointee->recordName.rfind("Outcome__nqg__",0)==0);
            if(!isResult||!sameReturn){
                diagnostics_.error("NQR-T3067",e->span,
                    "typed '?' requires a pointer to Result<T> or Outcome<T> and a matching function return type",
                    "write function process(input: *Result<int>): *Result<int> { let value = input?; return input }");
                return simple(TypeKind::Unknown);
            }
            const auto ok=resolveField(t,"ok"), value=resolveField(t,"value");
            if(!ok||!value||ok->size!=1||value->size==0||value->size>8){
                diagnostics_.error("NQR-T3068",e->span,
                    "typed '?' requires a scalar value and a bool ok field",
                    "use a Result or Outcome with an int, bool or pointer value; aggregate values need explicit handling");
                return simple(TypeKind::Unknown);
            }
            const Type payload=resolveType(value->typeName,e->span);
            if(!(payload.isInteger()||payload.kind==TypeKind::Bool||payload.kind==TypeKind::Pointer)){
                diagnostics_.error("NQR-T3068",e->span,
                    "typed '?' does not yet lower this value type safely",
                    "use a Result containing a scalar integer, bool or pointer");
                return simple(TypeKind::Unknown);
            }
            e->tryOkOffset=ok->offset;
            e->tryValueOffset=value->offset;
            e->tryValueWidth=value->size;
            return payload;
        }
        if(e->op==TokenKind::Try){if(!insideFunction_)diagnostics_.error("NOE-T3036",e->span,"try is only valid inside a function");if(!t.isInteger()&&t.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3037",e->span,"try expects an integer status value");if(!currentReturn_.isInteger()&&currentReturn_.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3038",e->span,"try propagation requires an integer-returning function");return t;}
        if(e->op==TokenKind::Ampersand){bool lvalue=std::dynamic_pointer_cast<NameExpr>(e->operand)||std::dynamic_pointer_cast<MemberExpr>(e->operand)||std::dynamic_pointer_cast<IndexExpr>(e->operand);if(auto u=std::dynamic_pointer_cast<UnaryExpr>(e->operand))lvalue=u->op==TokenKind::Star;if(!lvalue)diagnostics_.error("NOE-T3022",e->span,"address-of requires an addressable value");Type p;p.kind=TypeKind::Pointer;p.pointee=std::make_shared<Type>(t);return p;}
        if(e->op==TokenKind::Star){if(t.kind!=TypeKind::Pointer||!t.pointee){diagnostics_.error("NOE-T3023",e->span,"dereference requires a pointer");return simple(TypeKind::Unknown);}e->memoryWidth=std::max<std::size_t>(1,t.pointee->size());e->volatileAccess=t.isVolatile;e->memoryType=*t.pointee;return e->memoryType;}
        if(e->op==TokenKind::Bang){if(t.kind!=TypeKind::Bool&&t.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3006",e->span,"operator ! requires bool");return simple(TypeKind::Bool);}
        if(!t.isNumeric()&&t.kind!=TypeKind::Unknown&&t.kind!=TypeKind::Generic)diagnostics_.error("NOE-T3006",e->span,"numeric unary operator requires a numeric type");return t;
    }
    if(auto e=std::dynamic_pointer_cast<MemberExpr>(expr)){Type b=checkExpr(e->object);auto f=resolveField(b,e->member);if(!f){diagnostics_.error("NOE-T3024",e->span,"type '"+b.name()+"' has no field '"+e->member+"'");return simple(TypeKind::Unknown);}e->offset=f->offset;e->fieldSize=f->size;if(b.kind==TypeKind::Pointer){e->volatileAccess=b.isVolatile;e->baseIsPointer=true;}e->fieldType=resolveType(f->typeName,e->span);return e->fieldType;}
    if(auto e=std::dynamic_pointer_cast<IndexExpr>(expr)){
        Type container=checkExpr(e->object);Type idx=checkExpr(e->index);if(!idx.isInteger()&&idx.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3025",e->index->span,"index must be an integer");
        if(container.kind==TypeKind::Pointer&&container.pointee){e->elementSize=std::max<std::size_t>(1,container.pointee->size());e->volatileAccess=container.isVolatile;e->elementType=*container.pointee;return e->elementType;}
        if(container.kind==TypeKind::Array&&container.element){e->elementSize=std::max<std::size_t>(1,container.element->size());e->hasFixedBound=true;e->fixedBound=container.count;e->elementType=*container.element;return e->elementType;}
        if(container.kind==TypeKind::Slice&&container.element){e->elementSize=std::max<std::size_t>(1,container.element->size());e->baseIsSlice=true;e->elementType=*container.element;return e->elementType;}
        diagnostics_.error("NOE-T3026",e->span,"indexing requires a pointer, array or slice");return simple(TypeKind::Unknown);
    }
    if(auto e=std::dynamic_pointer_cast<CastExpr>(expr)){Type from=checkExpr(e->value),to=resolveType(e->typeName,e->span);bool ok=(from.isNumeric()&&to.isNumeric())||(from.kind==TypeKind::Pointer&&to.kind==TypeKind::Pointer)||(from.kind==TypeKind::Pointer&&(to.kind==TypeKind::Usize||to.kind==TypeKind::Isize))||(to.kind==TypeKind::Pointer&&(from.kind==TypeKind::Usize||from.kind==TypeKind::Isize||from.kind==TypeKind::Int))||(from.kind==TypeKind::Null&&to.kind==TypeKind::Pointer);if(!ok&&from.kind!=TypeKind::Unknown&&to.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3027",e->span,"cannot cast "+from.name()+" to "+to.name());return to;}
    if(auto e=std::dynamic_pointer_cast<BinaryExpr>(expr)){
        if(e->op==TokenKind::QuestionQuestion){
            Type l=checkExpr(e->left),r=checkExpr(e->right);
            if(l.kind==TypeKind::Pointer && (r==l || r.kind==TypeKind::Null))return l;
            if(l.kind==TypeKind::Null && r.kind==TypeKind::Pointer)return r;
            diagnostics_.error("NQR-T3062",e->span,
              "the ?? operator requires a nullable pointer and a matching pointer fallback",
              "use pointers of the same type, for example: value ?? fallback; use an explicit Result match for errors");
            return simple(TypeKind::Unknown);
        }
        if(e->op==TokenKind::Equal){Type lhs=checkExpr(e->left),rhs=checkExpr(e->right);if(auto n=std::dynamic_pointer_cast<NameExpr>(e->left))if(isConstSymbol(n->name))diagnostics_.error("NOE-T3014",e->span,"cannot assign to const '"+n->name+"'");if(!canAssignValue(lhs,rhs,e->right))diagnostics_.error("NOE-T3001",e->span,"cannot assign "+rhs.name()+" to "+lhs.name(),"use an explicit 'as' cast for narrowing or signedness-changing integer conversions");return lhs;}
        Type l=checkExpr(e->left),r=checkExpr(e->right);switch(e->op){case TokenKind::Plus:if(l.kind==TypeKind::String&&r.kind==TypeKind::String)return simple(TypeKind::String);if(l.kind==TypeKind::Pointer&&r.isInteger())return l;if(r.kind==TypeKind::Pointer&&l.isInteger())return r;[[fallthrough]];case TokenKind::Minus:case TokenKind::Star:case TokenKind::Slash:case TokenKind::Percent:
            if((!l.isNumeric()||!r.isNumeric())&&l.kind!=TypeKind::Unknown&&r.kind!=TypeKind::Unknown&&l.kind!=TypeKind::Generic&&r.kind!=TypeKind::Generic){diagnostics_.error("NOE-T3007",e->span,"arithmetic operator requires numeric operands");return simple(TypeKind::Unknown);}
            if(l.kind==TypeKind::Float||r.kind==TypeKind::Float)return simple(TypeKind::Float);
            if(l.kind==TypeKind::Unknown||r.kind==TypeKind::Unknown||l.kind==TypeKind::Generic||r.kind==TypeKind::Generic)return l;
            if(l.kind==r.kind)return l;
            if(l.isInteger()&&r.isInteger()){
                if(auto literal=literalInteger(e->right);literal&&literalFitsInteger(l,*literal))return l;
                if(auto literal=literalInteger(e->left);literal&&literalFitsInteger(r,*literal))return r;
                if(canAssign(l,r))return l;
                if(canAssign(r,l))return r;
                diagnostics_.error("NOE-T3055",e->span,"mixed integer arithmetic requires an explicit cast when neither operand can widen safely");
                return simple(TypeKind::Unknown);
            }
            return l;case TokenKind::Less:case TokenKind::LessEqual:case TokenKind::Greater:case TokenKind::GreaterEqual:case TokenKind::EqualEqual:case TokenKind::BangEqual:case TokenKind::AndAnd:case TokenKind::OrOr:return simple(TypeKind::Bool);default:return simple(TypeKind::Unknown);}
    }
    if(auto e=std::dynamic_pointer_cast<CallExpr>(expr)){
        std::string name=calleeName(e->callee);if(name.empty()){diagnostics_.error("NOE-T3010",e->span,"only named functions and simple namespaces are callable");return simple(TypeKind::Unknown);}
        if(name=="host"||name=="abi"){if(e->args.empty()){diagnostics_.error("NOE-T3015",e->span,name+" requires a service name");return simple(TypeKind::Unknown);}Type service=checkExpr(e->args[0]);if(service.kind!=TypeKind::String&&service.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3016",e->args[0]->span,"service name must be a string");for(std::size_t i=1;i<e->args.size();++i)checkExpr(e->args[i]);return simple(TypeKind::Unknown);}
        if(name=="sizeOf"||name=="alignOf"){
            if(e->args.size()!=1){
                diagnostics_.error("NQR-T3065",e->span,name+" expects one typed value","write sizeOf(value) or alignOf(value)");
                return simple(TypeKind::Usize);
            }
            Type t=checkExpr(e->args[0]);
            if(t.kind==TypeKind::Unknown||t.kind==TypeKind::Void||t.kind==TypeKind::Generic||
               t.kind==TypeKind::Null||t.size()==0){
                diagnostics_.error("NQR-T3066",e->args[0]->span,
                    "cannot determine the layout of "+t.name(),
                    "supply a concrete initialized value (generics specialize before layout)");
            } else {
                e->builtinCount=name=="sizeOf"?t.size():t.alignment();
                e->builtinCountKnown=true;
            }
            return simple(TypeKind::Usize);
        }
        if(name=="len"){
            if(e->args.size()!=1){diagnostics_.error("NOE-T3040",e->span,"len expects exactly one array or slice");return simple(TypeKind::Usize);}Type t=checkExpr(e->args[0]);if(t.kind==TypeKind::Array){e->builtinCount=t.count;e->builtinCountKnown=true;}else if(t.kind!=TypeKind::Slice)diagnostics_.error("NOE-T3041",e->args[0]->span,"len expects an array or slice");return simple(TypeKind::Usize);
        }
        if(name=="slice"){
            if(e->args.size()==1){Type a=checkExpr(e->args[0]);if(a.kind!=TypeKind::Array||!a.element){diagnostics_.error("NOE-T3042",e->span,"slice(array) expects a fixed array");return simple(TypeKind::Unknown);}e->builtinCount=a.count;e->builtinCountKnown=true;e->builtinWidth=std::max<std::size_t>(1,a.element->size());Type s;s.kind=TypeKind::Slice;s.element=a.element;return s;}
            if(e->args.size()==2){
                Type p=checkExpr(e->args[0]),n=checkExpr(e->args[1]);
                if(!n.isInteger()&&n.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3044",e->args[1]->span,"slice length must be an integer");
                Type s;s.kind=TypeKind::Slice;
                if(p.kind==TypeKind::Pointer&&p.pointee){e->builtinWidth=std::max<std::size_t>(1,p.pointee->size());s.element=p.pointee;return s;}
                if(p.kind==TypeKind::Array&&p.element){e->builtinWidth=std::max<std::size_t>(1,p.element->size());s.element=p.element;return s;}if(p.kind==TypeKind::Slice&&p.element){e->builtinWidth=std::max<std::size_t>(1,p.element->size());e->builtinSliceSource=true;s.element=p.element;return s;}
                diagnostics_.error("NOE-T3043",e->args[0]->span,"slice(value, length) expects a pointer, fixed array, or slice");return simple(TypeKind::Unknown);
            }
            diagnostics_.error("NOE-T3045",e->span,"slice expects either an array or pointer plus length");return simple(TypeKind::Unknown);
        }
        if(name=="atomic.load"||name=="atomicLoad"||name=="atomic.store"||name=="atomicStore"||name=="atomic.exchange"||name=="atomicExchange"||name=="atomic.compareExchange"||name=="atomicCompareExchange"){
            std::size_t expected=(name.find("compare")!=std::string::npos||name.find("Compare")!=std::string::npos)?3:((name.find("store")!=std::string::npos||name.find("Store")!=std::string::npos||name.find("exchange")!=std::string::npos||name.find("Exchange")!=std::string::npos)?2:1);
            if(e->args.size()!=expected){diagnostics_.error("NOE-T3046",e->span,"wrong argument count for atomic operation");return simple(TypeKind::Unknown);}Type p=checkExpr(e->args[0]);if(p.kind!=TypeKind::Pointer||!p.pointee){diagnostics_.error("NOE-T3047",e->args[0]->span,"atomic operation requires a pointer");return simple(TypeKind::Unknown);}Type value=*p.pointee;if((!value.isInteger()&&value.kind!=TypeKind::Pointer&&value.kind!=TypeKind::Bool)||value.size()>8){diagnostics_.error("NOE-T3048",e->args[0]->span,"atomic values must be integer, bool or pointer sized at most 8 bytes");}e->builtinWidth=std::max<std::size_t>(1,value.size());for(std::size_t i=1;i<e->args.size();++i){Type a=checkExpr(e->args[i]);if(!canAssignValue(value,a,e->args[i]))diagnostics_.error("NOE-T3049",e->args[i]->span,"atomic value type mismatch");}if(name.find("store")!=std::string::npos||name.find("Store")!=std::string::npos)return simple(TypeKind::Void);return value;
        }
        if(name=="atomic.fence"||name=="atomicFence"){if(!e->args.empty())diagnostics_.error("NOE-T3050",e->span,"atomic fence takes no arguments");return simple(TypeKind::Void);}
        if(name=="asm"){
            if(e->args.size()!=1||!literalString(e->args[0]))diagnostics_.error("NOE-T3051",e->span,"asm expects one string literal instruction");else e->builtinText=*literalString(e->args[0]);return simple(TypeKind::Void);
        }
        if(name=="intrinsic"){
            if(e->args.empty()||!literalString(e->args[0])){diagnostics_.error("NOE-T3052",e->span,"intrinsic expects a string literal intrinsic name");return simple(TypeKind::Unknown);}e->builtinText=*literalString(e->args[0]);for(std::size_t i=1;i<e->args.size();++i)checkExpr(e->args[i]);if(e->builtinText=="x86.rdtsc")return simple(TypeKind::U64);if(e->builtinText=="x86.pause"||e->builtinText=="x86.halt"||e->builtinText=="compiler.fence")return simple(TypeKind::Void);diagnostics_.error("NOE-T3053",e->span,"unknown intrinsic '"+e->builtinText+"'");return simple(TypeKind::Unknown);
        }
        auto it=functions_.find(name);if(it==functions_.end()){diagnostics_.error("NOE-T3011",e->span,"unknown function '"+name+"'");return simple(TypeKind::Unknown);}const auto&sig=it->second;if(name!="print"&&e->args.size()!=sig.params.size())diagnostics_.error("NOE-T3012",e->span,"wrong argument count for '"+name+"'");
        std::unordered_map<std::string,Type> bindings;
        for(std::size_t i=0;i<e->args.size();++i){Type a=checkExpr(e->args[i]);if(i<sig.params.size()){if(!sig.genericParams.empty()){if(!bindGeneric(sig.params[i],a,bindings))diagnostics_.error("NOE-T3054",e->args[i]->span,"generic argument does not match parameter "+sig.params[i].name());}else if(!canAssignValue(sig.params[i],a,e->args[i]))diagnostics_.error("NOE-T3013",e->args[i]->span,"argument type mismatch: expected "+sig.params[i].name()+", found "+a.name());}}
        return substituteGeneric(sig.result,bindings);
    }
    return simple(TypeKind::Unknown);
}
} // namespace noe
