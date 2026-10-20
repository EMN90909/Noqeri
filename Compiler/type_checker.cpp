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

TypeChecker::TypeChecker(Diagnostics& diagnostics):diagnostics_(diagnostics){pushScope();currentReturn_=simple(TypeKind::Void);}
Type TypeChecker::checkExprAgainst(const ExprPtr&expr,const Type&expected){
    if(auto array=std::dynamic_pointer_cast<ArrayExpr>(expr);array&&array->elements.empty()&&expected.kind==TypeKind::Array&&expected.element){
        if(expected.count!=0)diagnostics_.error("NOE-T3034",array->span,"empty array literal can initialize only a zero-length fixed array");
        array->elementSize=std::max<std::size_t>(1,expected.element->size());
        return expected;
    }
    return checkExpr(expr);
}
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
        Type ret=fn->returnType?resolveType(*fn->returnType,fn->span):simple(TypeKind::Void);
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
    for(const auto&stmt:program.statements)checkStmt(stmt);
    return!diagnostics_.hasErrors();
}

void TypeChecker::checkStmt(const StmtPtr&stmt){
    if(std::dynamic_pointer_cast<RecordStmt>(stmt)||std::dynamic_pointer_cast<ImportStmt>(stmt)||std::dynamic_pointer_cast<ModuleStmt>(stmt))return;
    if(auto s=std::dynamic_pointer_cast<LetStmt>(stmt)){
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
            Type init=checkExprAgainst(s->initializer,declared);
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
    if(auto s=std::dynamic_pointer_cast<ReturnStmt>(stmt)){Type v=s->value?checkExprAgainst(s->value,currentReturn_):simple(TypeKind::Void);if(!canAssignValue(currentReturn_,v,s->value))diagnostics_.error("NOE-T3005",s->span,"return type mismatch: expected "+currentReturn_.name()+", found "+v.name());return;}
    if(auto s=std::dynamic_pointer_cast<ThrowStmt>(stmt)){Type code=checkExpr(s->value);if(!insideFunction_)diagnostics_.error("NOE-T3031",s->span,"throw is only valid inside a function");if(!code.isInteger()&&code.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3032",s->span,"throw requires an integer error code");if(!currentReturn_.isInteger()&&currentReturn_.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3033",s->span,"throw requires an integer-returning function","use an integer status return such as isize when using lightweight try/throw");return;}
    if(auto s=std::dynamic_pointer_cast<FunctionStmt>(stmt)){
        auto sig=functions_[s->name];if(s->isExtern||s->isForward)return;
        Type saved=currentReturn_;bool savedInside=insideFunction_;auto savedGenerics=activeGenericParams_;const auto savedLoopDepth=loopDepth_;
        currentReturn_=sig.result;insideFunction_=true;loopDepth_=0;activeGenericParams_.clear();for(const auto&g:s->genericParams)activeGenericParams_.insert(g);
        pushScope();for(std::size_t i=0;i<s->params.size();++i)define(s->params[i].name,sig.params[i],false,s->params[i].span);for(const auto&x:s->body->statements)checkStmt(x);popScope();
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
    if(auto e=std::dynamic_pointer_cast<NameExpr>(expr)){auto t=resolve(e->name);if(t)return*t;if(functions_.count(e->name)||e->name=="host"||e->name=="abi")return simple(TypeKind::Unknown);diagnostics_.error("NOE-T3002",e->span,"unknown symbol '"+e->name+"'");return simple(TypeKind::Unknown);}
    if(auto e=std::dynamic_pointer_cast<UnaryExpr>(expr)){
        Type t=checkExpr(e->operand);
        if(e->op==TokenKind::Try){if(!insideFunction_)diagnostics_.error("NOE-T3036",e->span,"try is only valid inside a function");if(!t.isInteger()&&t.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3037",e->span,"try expects an integer status value");if(!currentReturn_.isInteger()&&currentReturn_.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3038",e->span,"try propagation requires an integer-returning function");return t;}
        if(e->op==TokenKind::Ampersand){bool lvalue=std::dynamic_pointer_cast<NameExpr>(e->operand)||std::dynamic_pointer_cast<MemberExpr>(e->operand)||std::dynamic_pointer_cast<IndexExpr>(e->operand);if(auto u=std::dynamic_pointer_cast<UnaryExpr>(e->operand))lvalue=u->op==TokenKind::Star;if(!lvalue)diagnostics_.error("NOE-T3022",e->span,"address-of requires an addressable value");Type p;p.kind=TypeKind::Pointer;p.pointee=std::make_shared<Type>(t);return p;}
        if(e->op==TokenKind::Star){if(t.kind!=TypeKind::Pointer||!t.pointee){diagnostics_.error("NOE-T3023",e->span,"dereference requires a pointer");return simple(TypeKind::Unknown);}e->memoryWidth=std::max<std::size_t>(1,t.pointee->size());e->volatileAccess=t.isVolatile;return*t.pointee;}
        if(e->op==TokenKind::Bang){if(t.kind!=TypeKind::Bool&&t.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3006",e->span,"operator ! requires bool");return simple(TypeKind::Bool);}
        if(!t.isNumeric()&&t.kind!=TypeKind::Unknown&&t.kind!=TypeKind::Generic)diagnostics_.error("NOE-T3006",e->span,"numeric unary operator requires a numeric type");return t;
    }
    if(auto e=std::dynamic_pointer_cast<MemberExpr>(expr)){Type b=checkExpr(e->object);auto f=resolveField(b,e->member);if(!f){diagnostics_.error("NOE-T3024",e->span,"type '"+b.name()+"' has no field '"+e->member+"'");return simple(TypeKind::Unknown);}e->offset=f->offset;e->fieldSize=f->size;if(b.kind==TypeKind::Pointer){e->volatileAccess=b.isVolatile;e->baseIsPointer=true;}return resolveType(f->typeName,e->span);}
    if(auto e=std::dynamic_pointer_cast<IndexExpr>(expr)){
        Type container=checkExpr(e->object);Type idx=checkExpr(e->index);if(!idx.isInteger()&&idx.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3025",e->index->span,"index must be an integer");
        if(container.kind==TypeKind::Pointer&&container.pointee){e->elementSize=std::max<std::size_t>(1,container.pointee->size());e->volatileAccess=container.isVolatile;return*container.pointee;}
        if(container.kind==TypeKind::Array&&container.element){e->elementSize=std::max<std::size_t>(1,container.element->size());return*container.element;}
        if(container.kind==TypeKind::Slice&&container.element){e->elementSize=std::max<std::size_t>(1,container.element->size());e->baseIsSlice=true;return*container.element;}
        diagnostics_.error("NOE-T3026",e->span,"indexing requires a pointer, array or slice");return simple(TypeKind::Unknown);
    }
    if(auto e=std::dynamic_pointer_cast<CastExpr>(expr)){Type from=checkExpr(e->value),to=resolveType(e->typeName,e->span);bool ok=(from.isNumeric()&&to.isNumeric())||(from.kind==TypeKind::Pointer&&to.kind==TypeKind::Pointer)||(from.kind==TypeKind::Pointer&&(to.kind==TypeKind::Usize||to.kind==TypeKind::Isize))||(to.kind==TypeKind::Pointer&&(from.kind==TypeKind::Usize||from.kind==TypeKind::Isize||from.kind==TypeKind::Int))||(from.kind==TypeKind::Null&&to.kind==TypeKind::Pointer);if(!ok&&from.kind!=TypeKind::Unknown&&to.kind!=TypeKind::Unknown)diagnostics_.error("NOE-T3027",e->span,"cannot cast "+from.name()+" to "+to.name());return to;}
    if(auto e=std::dynamic_pointer_cast<BinaryExpr>(expr)){
        if(e->op==TokenKind::Equal){Type lhs=checkExpr(e->left),rhs=checkExprAgainst(e->right,lhs);if(auto n=std::dynamic_pointer_cast<NameExpr>(e->left))if(isConstSymbol(n->name))diagnostics_.error("NOE-T3014",e->span,"cannot assign to const '"+n->name+"'");if(!canAssignValue(lhs,rhs,e->right))diagnostics_.error("NOE-T3001",e->span,"cannot assign "+rhs.name()+" to "+lhs.name(),"use an explicit 'as' cast for narrowing or signedness-changing integer conversions");return lhs;}
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
        for(std::size_t i=0;i<e->args.size();++i){Type a=(i<sig.params.size()&&sig.genericParams.empty())?checkExprAgainst(e->args[i],sig.params[i]):checkExpr(e->args[i]);if(i<sig.params.size()){if(!sig.genericParams.empty()){if(!bindGeneric(sig.params[i],a,bindings))diagnostics_.error("NOE-T3054",e->args[i]->span,"generic argument does not match parameter "+sig.params[i].name());}else if(!canAssignValue(sig.params[i],a,e->args[i]))diagnostics_.error("NOE-T3013",e->args[i]->span,"argument type mismatch: expected "+sig.params[i].name()+", found "+a.name());}}
        return substituteGeneric(sig.result,bindings);
    }
    return simple(TypeKind::Unknown);
}
} // namespace noe
