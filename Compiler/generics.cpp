#include "noe.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {

constexpr const char* GenericMarker="__nqg__";

std::string trim(std::string value){auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return{};auto last=value.find_last_not_of(" \t\r\n");return value.substr(first,last-first+1);}
bool ident(char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';}
bool identStart(char c){return std::isalpha(static_cast<unsigned char>(c))||c=='_';}
void skipSpace(const std::string&s,std::size_t&p){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}

std::vector<std::string> splitConstraints(const std::string& raw){std::vector<std::string> out;std::string current;for(char c:raw){if(c=='+'||c=='&'){auto v=trim(current);if(!v.empty())out.push_back(v);current.clear();}else current+=c;}auto v=trim(current);if(!v.empty())out.push_back(v);return out;}
std::string calleeName(const ExprPtr& expr){if(auto n=std::dynamic_pointer_cast<NameExpr>(expr))return n->name;return{};}
std::string safeType(const std::string& type){std::string out;for(char c:type)out+=ident(c)?c:'_';while(out.find("__")!=std::string::npos)out.replace(out.find("__"),2,"_");if(out.empty())out="unknown";return out;}

char hexDigit(unsigned value){return value<10?static_cast<char>('0'+value):static_cast<char>('a'+(value-10));}
int fromHex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return 10+c-'a';if(c>='A'&&c<='F')return 10+c-'A';return-1;}
std::string hexEncode(const std::string&value){std::string out;out.reserve(value.size()*2);for(unsigned char c:value){out+=hexDigit(c>>4);out+=hexDigit(c&15);}return out;}
std::optional<std::string> hexDecode(const std::string&value){if(value.size()%2)return std::nullopt;std::string out;out.reserve(value.size()/2);for(std::size_t i=0;i<value.size();i+=2){int hi=fromHex(value[i]),lo=fromHex(value[i+1]);if(hi<0||lo<0)return std::nullopt;out+=static_cast<char>((hi<<4)|lo);}return out;}

struct GenericApplication{std::string base;std::vector<std::string> args;};
std::string mangleGeneric(const std::string&base,const std::vector<std::string>&args){std::string out=base;for(const auto&arg:args)out+=std::string(GenericMarker)+hexEncode(trim(arg));return out;}
std::optional<GenericApplication> parseMangledGeneric(const std::string&value){const std::string marker=GenericMarker;auto first=value.find(marker);if(first==std::string::npos||first==0)return std::nullopt;GenericApplication app;app.base=value.substr(0,first);std::size_t p=first;while(p<value.size()){if(value.compare(p,marker.size(),marker)!=0)return std::nullopt;p+=marker.size();auto next=value.find(marker,p);auto encoded=value.substr(p,next==std::string::npos?std::string::npos:next-p);auto decoded=hexDecode(encoded);if(!decoded)return std::nullopt;app.args.push_back(*decoded);if(next==std::string::npos)break;p=next;}return app;}

std::string substitute(std::string value,const std::unordered_map<std::string,std::string>&bindings){
    std::string out;
    for(std::size_t i=0;i<value.size();){
        if(identStart(value[i])){
            std::size_t j=i+1;while(j<value.size()&&ident(value[j]))++j;
            auto word=value.substr(i,j-i);
            if(auto app=parseMangledGeneric(word)){
                std::vector<std::string> args;args.reserve(app->args.size());
                for(const auto&arg:app->args)args.push_back(substitute(arg,bindings));
                out+=mangleGeneric(app->base,args);
            }else{
                auto it=bindings.find(word);out+=it==bindings.end()?word:it->second;
            }
            i=j;
        }else out+=value[i++];
    }
    return out;
}

bool keywordAt(const std::string&s,std::size_t i,const std::string&kw){return i+kw.size()<=s.size()&&s.compare(i,kw.size(),kw)==0&&(i==0||!ident(s[i-1]))&&(i+kw.size()==s.size()||!ident(s[i+kw.size()]));}

std::optional<std::pair<std::string,std::size_t>> parseTypeText(const std::string&s,std::size_t start){
    std::size_t p=start;skipSpace(s,p);if(p>=s.size())return std::nullopt;
    if(s[p]=='*'){
        ++p;skipSpace(s,p);std::string prefix="*";
        if(keywordAt(s,p,"volatile")){prefix+="volatile ";p+=8;}
        auto nested=parseTypeText(s,p);if(!nested)return std::nullopt;return std::make_pair(prefix+nested->first,nested->second);
    }
    if(s[p]=='['){
        ++p;skipSpace(s,p);
        if(p<s.size()&&s[p]==']'){
            ++p;auto nested=parseTypeText(s,p);if(!nested)return std::nullopt;return std::make_pair("[]"+nested->first,nested->second);
        }
        auto nested=parseTypeText(s,p);if(!nested)return std::nullopt;p=nested->second;skipSpace(s,p);if(p>=s.size()||s[p]!=';')return std::nullopt;++p;skipSpace(s,p);std::size_t n=p;while(p<s.size()&&(std::isalnum(static_cast<unsigned char>(s[p]))||s[p]=='x'||s[p]=='X'))++p;if(n==p)return std::nullopt;auto count=s.substr(n,p-n);skipSpace(s,p);if(p>=s.size()||s[p]!=']')return std::nullopt;++p;return std::make_pair("["+nested->first+";"+count+"]",p);
    }
    if(!identStart(s[p]))return std::nullopt;
    std::size_t begin=p++;while(p<s.size()&&ident(s[p]))++p;std::string base=s.substr(begin,p-begin);
    const std::size_t afterName=p;
    skipSpace(s,p);
    // Preserve trailing whitespace/newlines when no generic arguments follow.
    // Otherwise rewriting `id: i64\nactive: bool` removed the newline and
    // produced the invalid token `i64active` (likewise `u8print` after casts).
    if(p>=s.size()||s[p]!='<')return std::make_pair(base,afterName);
    ++p;std::vector<std::string> args;
    for(;;){auto arg=parseTypeText(s,p);if(!arg)return std::nullopt;args.push_back(arg->first);p=arg->second;skipSpace(s,p);if(p<s.size()&&s[p]==','){++p;continue;}if(p<s.size()&&s[p]=='>'){++p;break;}return std::nullopt;}
    return std::make_pair(mangleGeneric(base,args),p);
}

struct Replacement{std::size_t begin=0,end=0;std::string text;};
void rewriteTypeApplications(std::string&source){
    std::vector<Replacement> replacements;bool inString=false,lineComment=false,blockComment=false,escape=false;
    for(std::size_t i=0;i<source.size();++i){char c=source[i],n=i+1<source.size()?source[i+1]:'\0';if(lineComment){if(c=='\n')lineComment=false;continue;}if(blockComment){if(c=='*'&&n=='/'){blockComment=false;++i;}continue;}if(inString){if(!escape&&c=='"')inString=false;escape=!escape&&c=='\\';if(c!='\\')escape=false;continue;}if(c=='"'){inString=true;continue;}if(c=='/'&&n=='/'){lineComment=true;++i;continue;}if(c=='/'&&n=='*'){blockComment=true;++i;continue;}
        std::size_t typeStart=std::string::npos;
        if(c==':')typeStart=i+1;
        else if(keywordAt(source,i,"as")){typeStart=i+2;i+=1;}
        if(typeStart==std::string::npos)continue;
        auto parsed=parseTypeText(source,typeStart);if(!parsed)continue;
        auto begin=typeStart;while(begin<source.size()&&std::isspace(static_cast<unsigned char>(source[begin])))++begin;
        auto original=source.substr(begin,parsed->second-begin);if(original!=parsed->first)replacements.push_back({begin,parsed->second,parsed->first});i=parsed->second?parsed->second-1:i;
    }
    for(auto it=replacements.rbegin();it!=replacements.rend();++it)source.replace(it->begin,it->end-it->begin,it->text);
}

void scanGenericDeclarations(const std::string&source,GenericSyntaxInfo&info){
    bool inString=false,lineComment=false,blockComment=false,escape=false;
    for(std::size_t i=0;i<source.size();++i){char c=source[i],n=i+1<source.size()?source[i+1]:'\0';if(lineComment){if(c=='\n')lineComment=false;continue;}if(blockComment){if(c=='*'&&n=='/'){blockComment=false;++i;}continue;}if(inString){if(!escape&&c=='"')inString=false;escape=!escape&&c=='\\';if(c!='\\')escape=false;continue;}if(c=='"'){inString=true;continue;}if(c=='/'&&n=='/'){lineComment=true;++i;continue;}if(c=='/'&&n=='*'){blockComment=true;++i;continue;}
        bool isFunction=keywordAt(source,i,"function"),isRecord=keywordAt(source,i,"record");if(!isFunction&&!isRecord)continue;const std::string kw=isFunction?"function":"record";std::size_t p=i+kw.size();skipSpace(source,p);std::size_t ns=p;while(p<source.size()&&ident(source[p]))++p;if(p==ns)continue;auto name=source.substr(ns,p-ns);skipSpace(source,p);if(p>=source.size()||source[p]!='<')continue;std::size_t angleStart=p;int depth=0;std::size_t end=p;for(;end<source.size();++end){if(source[end]=='<')++depth;else if(source[end]=='>'&&--depth==0)break;}if(end>=source.size())continue;
        std::vector<std::string> params;std::size_t seg=p+1;while(seg<end){std::size_t comma=seg,localDepth=0;for(;comma<end;++comma){if(source[comma]=='<')++localDepth;else if(source[comma]=='>')--localDepth;else if(source[comma]==','&&localDepth==0)break;}auto part=source.substr(seg,comma-seg);auto colon=part.find(':');auto gp=trim(part.substr(0,colon));if(!gp.empty())params.push_back(gp);if(isFunction&&colon!=std::string::npos){auto constraints=splitConstraints(part.substr(colon+1));if(!gp.empty()&&!constraints.empty())info.constraints[name][gp]=constraints;std::size_t blankStart=seg+colon;for(std::size_t x=blankStart;x<comma;++x)if(info.source[x]!='\n'&&info.source[x]!='\r')info.source[x]=' ';}seg=comma+1;}
        if(isRecord){info.recordParams[name]=params;for(std::size_t x=angleStart;x<=end;++x)if(info.source[x]!='\n'&&info.source[x]!='\r')info.source[x]=' ';}
        i=end;
    }
}

ExprPtr cloneExpr(const ExprPtr& e){
    if(!e)return{};
    if(auto x=std::dynamic_pointer_cast<LiteralExpr>(e))return std::make_shared<LiteralExpr>(*x);
    if(auto x=std::dynamic_pointer_cast<NameExpr>(e))return std::make_shared<NameExpr>(*x);
    if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e)){auto n=std::make_shared<UnaryExpr>(*x);n->operand=cloneExpr(x->operand);return n;}
    if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){auto n=std::make_shared<BinaryExpr>(*x);n->left=cloneExpr(x->left);n->right=cloneExpr(x->right);return n;}
    if(auto x=std::dynamic_pointer_cast<CallExpr>(e)){auto n=std::make_shared<CallExpr>(*x);n->callee=cloneExpr(x->callee);n->args.clear();for(const auto&a:x->args)n->args.push_back(cloneExpr(a));return n;}
    if(auto x=std::dynamic_pointer_cast<CastExpr>(e)){auto n=std::make_shared<CastExpr>(*x);n->value=cloneExpr(x->value);return n;}
    if(auto x=std::dynamic_pointer_cast<IndexExpr>(e)){auto n=std::make_shared<IndexExpr>(*x);n->object=cloneExpr(x->object);n->index=cloneExpr(x->index);return n;}
    if(auto x=std::dynamic_pointer_cast<MemberExpr>(e)){auto n=std::make_shared<MemberExpr>(*x);n->object=cloneExpr(x->object);return n;}
    if(auto x=std::dynamic_pointer_cast<ArrayExpr>(e)){auto n=std::make_shared<ArrayExpr>(*x);n->elements.clear();for(const auto&a:x->elements)n->elements.push_back(cloneExpr(a));return n;}
    return{};
}

StmtPtr cloneStmt(const StmtPtr& s){
    if(!s)return{};
    if(auto x=std::dynamic_pointer_cast<ExprStmt>(s)){auto n=std::make_shared<ExprStmt>(*x);n->expr=cloneExpr(x->expr);return n;}
    if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){auto n=std::make_shared<LetStmt>(*x);n->initializer=cloneExpr(x->initializer);return n;}
    if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){auto n=std::make_shared<BlockStmt>(*x);n->statements.clear();for(const auto&i:x->statements)n->statements.push_back(cloneStmt(i));return n;}
    if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){auto n=std::make_shared<IfStmt>(*x);n->condition=cloneExpr(x->condition);n->thenBranch=cloneStmt(x->thenBranch);n->elseBranch=cloneStmt(x->elseBranch);return n;}
    if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){auto n=std::make_shared<WhileStmt>(*x);n->condition=cloneExpr(x->condition);n->body=cloneStmt(x->body);return n;}
    if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s)){auto n=std::make_shared<ReturnStmt>(*x);n->value=cloneExpr(x->value);return n;}
    if(auto x=std::dynamic_pointer_cast<ThrowStmt>(s)){auto n=std::make_shared<ThrowStmt>(*x);n->value=cloneExpr(x->value);return n;}
    if(auto x=std::dynamic_pointer_cast<RecordStmt>(s))return std::make_shared<RecordStmt>(*x);
    if(auto x=std::dynamic_pointer_cast<ImportStmt>(s))return std::make_shared<ImportStmt>(*x);
    if(auto x=std::dynamic_pointer_cast<ModuleStmt>(s))return std::make_shared<ModuleStmt>(*x);
    return s;
}

void substituteStmt(const StmtPtr&s,const std::unordered_map<std::string,std::string>&b){if(!s)return;if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){if(x->annotation)x->annotation=substitute(*x->annotation,b);}else if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){for(auto&i:x->statements)substituteStmt(i,b);}else if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){substituteStmt(x->thenBranch,b);substituteStmt(x->elseBranch,b);}else if(auto x=std::dynamic_pointer_cast<WhileStmt>(s))substituteStmt(x->body,b);}
std::string elementType(const std::string&t){if(t.rfind("[]",0)==0)return t.substr(2);if(t.rfind("*volatile ",0)==0)return t.substr(10);if(t.rfind("*",0)==0)return t.substr(1);if(t.size()>3&&t.front()=='['){auto semi=t.rfind(';');if(semi!=std::string::npos)return t.substr(1,semi-1);}return"unknown";}
bool capability(const std::string&type,const std::string&c){const Type t=typeFromName(type);if(c=="Eq")return t.kind!=TypeKind::Void&&t.kind!=TypeKind::Unknown;if(c=="Ord")return t.isNumeric()||t.kind==TypeKind::String||t.kind==TypeKind::Bool;if(c=="Numeric")return t.isNumeric();if(c=="Integer")return t.isInteger();if(c=="Copy")return !t.isAggregate()&&t.kind!=TypeKind::Void&&t.kind!=TypeKind::Unknown;return false;}

class Monomorphizer {
public:
    Monomorphizer(Program&program,Diagnostics&diagnostics):program_(program),diagnostics_(diagnostics){
        for(const auto&s:program.statements){if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s))functions_[f->name]=f;else if(auto r=std::dynamic_pointer_cast<RecordStmt>(s);r&&!r->genericParams.empty())recordTemplates_[r->name]=r;}
    }

    bool run(){
        std::vector<StmtPtr> nongeneric;
        for(const auto&s:program_.statements){
            if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s);f&&!f->genericParams.empty())continue;
            if(auto r=std::dynamic_pointer_cast<RecordStmt>(s);r&&!r->genericParams.empty())continue;
            nongeneric.push_back(s);
        }
        for(auto&s:nongeneric){
            if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s)){for(const auto&p:f->params)if(p.annotation)ensureType(*p.annotation);if(f->returnType)ensureType(*f->returnType);Env env;for(const auto&p:f->params)if(p.annotation)env[p.name]=*p.annotation;processStmt(f->body,env);}
            else if(auto r=std::dynamic_pointer_cast<RecordStmt>(s)){for(const auto&field:r->fields)ensureType(field.typeName);}
            else{Env env;processStmt(s,env);}
        }
        std::vector<StmtPtr> result;result.reserve(nongeneric.size()+recordInstances_.size()+instances_.size());
        result.insert(result.end(),nongeneric.begin(),nongeneric.end());
        result.insert(result.end(),recordInstances_.begin(),recordInstances_.end());
        result.insert(result.end(),instances_.begin(),instances_.end());
        program_.statements=std::move(result);
        return!diagnostics_.hasErrors();
    }

private:
    using Env=std::unordered_map<std::string,std::string>;

    void ensureType(const std::string&type){
        if(type.rfind("*volatile ",0)==0){ensureType(type.substr(10));return;}if(type.rfind("*",0)==0){ensureType(type.substr(1));return;}if(type.rfind("[]",0)==0){ensureType(type.substr(2));return;}if(type.size()>3&&type.front()=='['){auto semi=type.rfind(';');if(semi!=std::string::npos)ensureType(type.substr(1,semi-1));return;}
        auto app=parseMangledGeneric(type);if(!app)return;for(const auto&arg:app->args)ensureType(arg);auto templ=recordTemplates_.find(app->base);if(templ==recordTemplates_.end())return;
        auto key=mangleGeneric(app->base,app->args);if(recordCache_.count(key))return;recordCache_.insert(key);
        auto source=templ->second;if(source->genericParams.size()!=app->args.size()){diagnostics_.error("NQR-G4010",source->span,"generic record '"+source->name+"' expects "+std::to_string(source->genericParams.size())+" type arguments, found "+std::to_string(app->args.size()));return;}
        std::unordered_map<std::string,std::string> bindings;for(std::size_t i=0;i<source->genericParams.size();++i)bindings[source->genericParams[i]]=app->args[i];
        auto instance=std::make_shared<RecordStmt>(*source);instance->name=key;instance->genericParams.clear();instance->fields=source->fields;for(auto&field:instance->fields){field.typeName=substitute(field.typeName,bindings);ensureType(field.typeName);}recordInstances_.push_back(instance);
    }

    std::string memberType(const std::string&raw,const std::string&member){
        auto type=raw;if(type.rfind("*volatile ",0)==0)type=type.substr(10);else if(type.rfind("*",0)==0)type=type.substr(1);auto app=parseMangledGeneric(type);if(!app)return"unknown";auto templ=recordTemplates_.find(app->base);if(templ==recordTemplates_.end()||templ->second->genericParams.size()!=app->args.size())return"unknown";std::unordered_map<std::string,std::string> bindings;for(std::size_t i=0;i<app->args.size();++i)bindings[templ->second->genericParams[i]]=app->args[i];for(const auto&field:templ->second->fields)if(field.name==member)return substitute(field.typeName,bindings);return"unknown";
    }

    std::string infer(const ExprPtr&e,Env&env){
        if(!e)return"void";
        if(auto x=std::dynamic_pointer_cast<LiteralExpr>(e)){if(std::holds_alternative<std::int64_t>(x->value))return"int";if(std::holds_alternative<double>(x->value))return"float";if(std::holds_alternative<bool>(x->value))return"bool";if(std::holds_alternative<std::string>(x->value))return"string";return"null";}
        if(auto x=std::dynamic_pointer_cast<NameExpr>(e)){auto i=env.find(x->name);return i==env.end()?"unknown":i->second;}
        if(auto x=std::dynamic_pointer_cast<CastExpr>(e)){ensureType(x->typeName);return x->typeName;}
        if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e)){auto t=infer(x->operand,env);if(x->op==TokenKind::Bang)return"bool";if(x->op==TokenKind::Ampersand)return"*"+t;if(x->op==TokenKind::Star)return elementType(t);return t;}
        if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){auto l=infer(x->left,env);infer(x->right,env);switch(x->op){case TokenKind::EqualEqual:case TokenKind::BangEqual:case TokenKind::Less:case TokenKind::LessEqual:case TokenKind::Greater:case TokenKind::GreaterEqual:case TokenKind::AndAnd:case TokenKind::OrOr:return"bool";default:return l;}}
        if(auto x=std::dynamic_pointer_cast<ArrayExpr>(e)){auto t=x->elements.empty()?"unknown":infer(x->elements.front(),env);return"["+t+";"+std::to_string(x->elements.size())+"]";}
        if(auto x=std::dynamic_pointer_cast<IndexExpr>(e))return elementType(infer(x->object,env));
        if(auto x=std::dynamic_pointer_cast<MemberExpr>(e))return memberType(infer(x->object,env),x->member);
        if(auto x=std::dynamic_pointer_cast<CallExpr>(e))return processCall(x,env);
        return"unknown";
    }

    bool bind(const std::string&pattern,const std::string&actual,const std::vector<std::string>&gps,std::unordered_map<std::string,std::string>&bindings){
        for(const auto&gp:gps){if(pattern==gp){auto it=bindings.find(gp);if(it==bindings.end()){bindings[gp]=actual;return true;}return it->second==actual;}if(pattern=="*"+gp&&actual.rfind("*",0)==0)return bind(gp,elementType(actual),gps,bindings);if(pattern=="[]"+gp&&actual.rfind("[]",0)==0)return bind(gp,elementType(actual),gps,bindings);}
        auto p=parseMangledGeneric(pattern),a=parseMangledGeneric(actual);if(p||a){if(!p||!a||p->base!=a->base||p->args.size()!=a->args.size())return false;for(std::size_t i=0;i<p->args.size();++i)if(!bind(p->args[i],a->args[i],gps,bindings))return false;return true;}
        if(pattern.rfind("*",0)==0&&actual.rfind("*",0)==0)return bind(elementType(pattern),elementType(actual),gps,bindings);
        if(pattern.rfind("[]",0)==0&&actual.rfind("[]",0)==0)return bind(elementType(pattern),elementType(actual),gps,bindings);
        return pattern==actual||typeFromName(pattern).kind==TypeKind::Unknown;
    }

    std::string processCall(const std::shared_ptr<CallExpr>&call,Env&env){
        auto name=calleeName(call->callee);std::vector<std::string>args;for(auto&a:call->args)args.push_back(infer(a,env));auto fit=functions_.find(name);if(fit==functions_.end())return name=="len"?"usize":"unknown";auto fn=fit->second;if(fn->genericParams.empty())return fn->returnType.value_or("void");std::unordered_map<std::string,std::string> bindings;for(std::size_t i=0;i<fn->params.size()&&i<args.size();++i)if(fn->params[i].annotation&&!bind(*fn->params[i].annotation,args[i],fn->genericParams,bindings))diagnostics_.error("NQR-G4001",call->span,"generic arguments for '"+name+"' disagree");for(const auto&gp:fn->genericParams)if(!bindings.count(gp))diagnostics_.error("NQR-G4002",call->span,"cannot infer generic parameter '"+gp+"' for '"+name+"'");if(diagnostics_.hasErrors())return"unknown";for(const auto&[gp,requirements]:fn->genericConstraints){auto it=bindings.find(gp);if(it==bindings.end())continue;for(const auto&req:requirements)if(!capability(it->second,req))diagnostics_.error("NQR-G4003",call->span,"type '"+it->second+"' does not satisfy constraint '"+req+"' for '"+gp+"'");}if(diagnostics_.hasErrors())return"unknown";auto instance=instantiate(fn,bindings);if(auto callee=std::dynamic_pointer_cast<NameExpr>(call->callee))callee->name=instance;auto result=substitute(fn->returnType.value_or("void"),bindings);ensureType(result);return result;
    }

    std::string instantiate(const std::shared_ptr<FunctionStmt>&fn,const std::unordered_map<std::string,std::string>&bindings){
        std::ostringstream key,name;key<<fn->name;name<<fn->name;for(const auto&gp:fn->genericParams){auto type=bindings.at(gp);key<<'|'<<type;name<<"__"<<safeType(type);}auto k=key.str();if(auto i=cache_.find(k);i!=cache_.end())return i->second;auto instance=name.str();cache_[k]=instance;auto copy=std::make_shared<FunctionStmt>(*fn);copy->name=instance;copy->genericParams.clear();copy->genericConstraints.clear();copy->params=fn->params;for(auto&p:copy->params)if(p.annotation){p.annotation=substitute(*p.annotation,bindings);ensureType(*p.annotation);}if(copy->returnType){copy->returnType=substitute(*copy->returnType,bindings);ensureType(*copy->returnType);}copy->body=std::dynamic_pointer_cast<BlockStmt>(cloneStmt(fn->body));substituteStmt(copy->body,bindings);instances_.push_back(copy);Env env;for(const auto&p:copy->params)if(p.annotation)env[p.name]=*p.annotation;processStmt(copy->body,env);return instance;
    }

    void processStmt(const StmtPtr&s,Env&env){
        if(!s)return;
        if(auto x=std::dynamic_pointer_cast<ExprStmt>(s)){infer(x->expr,env);return;}
        if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){auto t=infer(x->initializer,env);if(x->annotation)ensureType(*x->annotation);env[x->name]=x->annotation.value_or(t);return;}
        if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s)){infer(x->value,env);return;}
        if(auto x=std::dynamic_pointer_cast<ThrowStmt>(s)){infer(x->value,env);return;}
        if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){Env nested=env;for(auto&i:x->statements)processStmt(i,nested);return;}
        if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){infer(x->condition,env);Env a=env,b=env;processStmt(x->thenBranch,a);processStmt(x->elseBranch,b);return;}
        if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){infer(x->condition,env);Env nested=env;processStmt(x->body,nested);return;}
    }

    Program&program_;Diagnostics&diagnostics_;
    std::unordered_map<std::string,std::shared_ptr<FunctionStmt>> functions_;
    std::unordered_map<std::string,std::shared_ptr<RecordStmt>> recordTemplates_;
    std::unordered_map<std::string,std::string> cache_;
    std::unordered_set<std::string> recordCache_;
    std::vector<StmtPtr> recordInstances_;
    std::vector<StmtPtr> instances_;
};

} // namespace

GenericSyntaxInfo preprocessGenericSyntax(const std::string&source){GenericSyntaxInfo info;info.source=source;scanGenericDeclarations(source,info);rewriteTypeApplications(info.source);return info;}
void applyGenericSyntax(Program&program,const GenericSyntaxInfo&syntax){for(auto&s:program.statements){if(auto fn=std::dynamic_pointer_cast<FunctionStmt>(s)){auto it=syntax.constraints.find(fn->name);if(it!=syntax.constraints.end())fn->genericConstraints=it->second;}else if(auto record=std::dynamic_pointer_cast<RecordStmt>(s)){auto it=syntax.recordParams.find(record->name);if(it!=syntax.recordParams.end())record->genericParams=it->second;}}}
bool GenericEngine::monomorphize(Program&program,Diagnostics&diagnostics)const{return Monomorphizer(program,diagnostics).run();}

} // namespace noe
