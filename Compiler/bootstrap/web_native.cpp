// SPDX-License-Identifier: GPL-3.0-only
// Native Noqeri web renderer: no Node.js, JS engine, npm, or browser DOM runtime.
// This deliberately compiles a *static*, fail-closed subset of Noqeri views.
// Dynamic events/data loaders require an independently designed native backend.
#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <cstring>
#include <type_traits>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dlfcn.h>
#endif
#include "web_native_noesql_v2.hpp"
#ifdef NOQERI_WEB_SQLITE
#include <sqlite3.h>
#endif
#ifdef NOQERI_WEB_POSTGRES
#include <libpq-fe.h>
#endif
#ifdef NOQERI_WEB_DUCKDB
#include <duckdb.h>
#endif
#ifdef NOQERI_WEB_MYSQL
#include <mysql.h>
#endif

namespace fs = std::filesystem;
namespace {
struct Token { std::string value; bool quoted = false; std::size_t line = 1; };
struct Atom { std::string value; bool variable = false; };
struct Expr { std::vector<Atom> atoms; };
struct DbConfiguration {
    std::string engine="noesql", path, dsnEnvironment, binaryEnvironment="NOESQL_BIN", keyEnvironment;
};
struct DbParameterExpr {
    std::string name;
    Expr value;
};
struct Mutation {
    std::string query;
    std::vector<DbParameterExpr> dbParams;
    std::string target, source, op, field;
    Expr value;
    std::size_t minLength=0,maxLength=0;
};
struct Node {
    std::string actionId, eventName;
    std::vector<Mutation> mutations;
    std::string tag, kind; // "text", "element", "component"
    Expr text;
    std::vector<std::pair<std::string,Expr>> attrs;
    std::vector<Expr> args;
    std::vector<Node> children;
};
struct Component { std::vector<std::string> params; std::vector<Node> children; };
struct Project {
    std::map<std::string,std::string> globals;
    std::set<std::string> mutableState, persistentState;
    std::size_t nextActionId = 0;
    std::map<std::string,std::string> theme;
    DbConfiguration database;
    bool databaseConfigured=false;
    std::map<std::string,std::string> queries;
    std::map<std::string,Component> components;
    std::map<std::string,std::vector<Node>> routes;
    std::vector<Node> fallback;
    std::vector<Node> layout;
    bool hasLayout=false;
    std::string uiAlias;
    bool hasFallback = false;
};
std::string readText(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path,ec) || fs::file_size(path,ec) > 1024*1024)
        throw std::runtime_error("missing or oversized Noqeri source: " + path.string());
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream out; out << stream.rdbuf();
    return out.str();
}
bool wordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c=='_'; }
bool wordContinue(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c=='_'; }
std::vector<Token> lex(const std::string& source) {
    std::vector<Token> out;
    std::size_t i=0,line=1;
    auto add=[&](std::string v,bool q=false) {
        if (out.size()>=50000) throw std::runtime_error("web-native token limit exceeded");
        out.push_back({std::move(v),q,line});
    };
    while (i<source.size()) {
        const char c=source[i];
        if (c=='\n') { ++line; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c=='/' && i+1<source.size() && source[i+1]=='/') {
            while (i<source.size() && source[i]!='\n') ++i;
            continue;
        }
        if (c=='/' && i+1<source.size() && source[i+1]=='*') {
            i+=2; bool terminated=false;
            while (i+1<source.size()) {
                if (source[i]=='*' && source[i+1]=='/') { i+=2; terminated=true; break; }
                if (source[i]=='\n') ++line;
                ++i;
            }
            if (!terminated) throw std::runtime_error("unterminated Noqeri block comment");
            continue;
        }
        if (c=='"') {
            ++i; std::string value; bool done=false;
            while (i<source.size()) {
                char next=source[i++];
                if (next=='"') { done=true; break; }
                if (next=='\n' || next=='\r') throw std::runtime_error("newline in Noqeri string");
                if (next=='\\') {
                    if (i==source.size()) break;
                    const char escaped=source[i++];
                    switch (escaped) {
                        case 'n': value+='\n'; break;
                        case 'r': value+='\r'; break;
                        case 't': value+='\t'; break;
                        case '"': value+='"'; break;
                        case '\\': value+='\\'; break;
                        default: throw std::runtime_error("unsupported string escape");
                    }
                } else value+=next;
            }
            if (!done) throw std::runtime_error("unterminated Noqeri string");
            add(std::move(value),true); continue;
        }
        if (wordStart(c)) {
            std::size_t start=i++;
            while (i<source.size() && wordContinue(source[i])) ++i;
            add(source.substr(start,i-start)); continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start=i++;
            while (i<source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            add(source.substr(start,i-start)); continue;
        }
        if (std::string("{}().,:;=+/-").find(c)!=std::string::npos) { add(std::string(1,c)); ++i; continue; }
        throw std::runtime_error("unsupported web-native token at line "+std::to_string(line));
    }
    out.push_back({"<eof>",false,line});
    return out;
}
bool validName(const std::string& x) {
    if (x.empty()||!wordStart(x[0])) return false;
    return std::all_of(x.begin()+1,x.end(),wordContinue);
}
std::string evaluate(const Expr& e,const std::map<std::string,std::string>& locals,
                     const std::map<std::string,std::string>& globals) {
    std::string value;
    for (const auto& atom:e.atoms) {
        if (!atom.variable) { value+=atom.value; continue; }
        const auto found=locals.find(atom.value);
        if (found!=locals.end()) { value+=found->second; continue; }
        const auto global=globals.find(atom.value);
        if (global!=globals.end()) { value+=global->second; continue; }
        throw std::runtime_error("unknown static expression name: "+atom.value);
    }
    return value;
}
class Parser {
    std::vector<Token> tokens;
    std::size_t pos=0, nodeCount=0;
    Project& p;
    bool library;
    const Token& peek() const { return tokens.at(pos); }
    bool take(const std::string& s) {
        if (!peek().quoted && peek().value==s) { ++pos; return true; }
        return false;
    }
    void expect(const std::string& s) {
        if (!take(s)) fail("expected '"+s+"'");
    }
    [[noreturn]] void fail(const std::string& msg) const {
        throw std::runtime_error("web-native line "+std::to_string(peek().line)+": "+msg);
    }
    std::string identifier() {
        if (peek().quoted||!validName(peek().value)) fail("expected identifier");
        return tokens.at(pos++).value;
    }
    std::string literalString() {
        if (!peek().quoted) fail("expected quoted string");
        return tokens.at(pos++).value;
    }
    Expr expression() {
        Expr result;
        do {
            if (peek().quoted) result.atoms.push_back({tokens.at(pos++).value,false});
            else if (validName(peek().value) && peek().value!="true" && peek().value!="false")
                result.atoms.push_back({tokens.at(pos++).value,true});
            else if (peek().value=="true"||peek().value=="false" ||
                (!peek().value.empty() && std::isdigit(static_cast<unsigned char>(peek().value[0]))))
                result.atoms.push_back({tokens.at(pos++).value,false});
            else fail("expected a static literal or variable");
        } while (take("+"));
        return result;
    }

    std::vector<Mutation> mutations() {
        expect("{");
        std::vector<Mutation> list;
        while (!take("}")) {
            if (list.size()>=8) fail("too many mutations in event");
            Mutation m; m.target=identifier();
            if(m.target=="validate") {
                expect("(");m.field=identifier();expect(",");
                if(peek().quoted||peek().value.empty()||
                   !std::all_of(peek().value.begin(),peek().value.end(),
                     [](char c){return c>='0'&&c<='9';}))fail("validation minimum must be an integer");
                m.minLength=static_cast<std::size_t>(std::stoul(tokens.at(pos++).value));
                expect(",");
                if(peek().quoted||peek().value.empty()||
                   !std::all_of(peek().value.begin(),peek().value.end(),
                     [](char c){return c>='0'&&c<='9';}))fail("validation maximum must be an integer");
                m.maxLength=static_cast<std::size_t>(std::stoul(tokens.at(pos++).value));
                expect(")");
                if(m.maxLength>256||m.minLength>m.maxLength)fail("invalid validation length bounds");
                m.op="validate";
            } else if(m.target=="validateEmail") {
                expect("(");m.field=identifier();expect(")");
                m.op="validate_email";
            } else if(m.target=="validateNumber") {
                expect("(");m.field=identifier();expect(",");
                const auto number=[&]()->std::size_t {
                    if(peek().quoted||peek().value.empty()||
                       !std::all_of(peek().value.begin(),peek().value.end(),
                           [](char ch){return ch>='0'&&ch<='9';}))
                        fail("number validation bound must be a positive integer");
                    return static_cast<std::size_t>(std::stoul(tokens.at(pos++).value));
                };
                m.minLength=number();expect(",");m.maxLength=number();expect(")");
                if(m.maxLength>1000000||m.minLength>m.maxLength)
                    fail("number validation bounds must satisfy 0 <= min <= max <= 1000000");
                m.op="validate_number";
            } else if(m.target=="db") {
                expect("."); expect("execute"); expect("(");
                m.op="db"; m.query=literalString();
                while(take(",")) {
                    DbParameterExpr param; param.name=identifier(); expect(":");
                    if(take("form")) {
                        expect("."); const auto field=identifier();
                        if(field.rfind("_nq_",0)==0)fail("reserved form field");
                        param.value.atoms.push_back({"form."+field,true});
                    } else param.value=expression();
                    m.dbParams.push_back(std::move(param));
                    if(m.dbParams.size()>32)fail("too many database binds");
                }
                expect(")");
            } else {
                expect("=");
                if (take("form")) {
                expect("."); m.field=identifier();
                if (m.field.rfind("_nq_",0)==0) fail("reserved form field");
                m.op="field";
            } else {
                m.value=expression();
                m.op="set";
                // The integer syntax name = name + number is strictly arithmetic.
                if (m.value.atoms.size()==2 && m.value.atoms[0].variable &&
                    m.value.atoms[0].value==m.target && !m.value.atoms[1].variable &&
                    !m.value.atoms[1].value.empty() &&
                    std::all_of(m.value.atoms[1].value.begin(),m.value.atoms[1].value.end(),
                        [](char c){return c>='0'&&c<='9';})) m.op="add";
            }
            }
            list.push_back(std::move(m)); take(";");
        }
        if (list.empty()) fail("event must modify at least one state variable");
        return list;
    }
    std::vector<Node> children(int depth=0) {
        if (depth>48) fail("element nesting limit exceeded");
        expect("{"); std::vector<Node> result;
        while (!take("}")) {
            if (peek().value=="<eof>") fail("unterminated view");
            if (take(";")) continue;
            if (++nodeCount>20000) fail("view node limit exceeded");
            Node n;
            if (peek().quoted) { n.kind="text"; n.text=expression(); }
            else {
                std::string name=identifier();
                if (take(".")) name+="."+identifier();
                if (name=="slot"&&peek().value=="{") {
                    n.kind="slot";expect("{");expect("}");
                } else if (take("(")) {
                    n.kind="component"; n.tag=name;
                    if (!take(")")) {
                        do { n.args.push_back(expression()); } while (take(","));
                        expect(")");
                    }
                } else if (peek().value=="{" || peek().value=="on" ||
                           (validName(peek().value) && tokens.at(pos+1).value==":")) {
                    n.kind="element"; n.tag=name;
                    while (peek().value!="{") {
                        if (take("on")) {
                            expect("."); n.eventName=identifier();
                            if (!n.actionId.empty() || (n.eventName!="click" && n.eventName!="submit") ||
                                (n.eventName=="click"&&n.tag!="button") ||
                                (n.eventName=="submit"&&n.tag!="form"))
                                fail("native event must be button on.click or form on.submit");
                            n.actionId=std::to_string(++p.nextActionId);
                            n.mutations=mutations();
                            continue;
                        }
                        const auto key=identifier();
                        expect(":"); n.attrs.push_back({key,expression()});
                    }
                    n.children=children(depth+1);
                } else {
                    n.kind="text"; n.text.atoms.push_back({name,true});
                    while (take("+")) {
                        const auto more=expression();
                        n.text.atoms.insert(n.text.atoms.end(),more.atoms.begin(),more.atoms.end());
                    }
                }
            }
            result.push_back(std::move(n));
            take(";");
        }
        return result;
    }
    void component() {
        const auto name=identifier();
        expect("("); Component c;
        if (!take(")")) {
            do {
                const auto param=identifier();
                if (take(":")) {
                    const auto type=identifier();
                    if (type!="string" && type!="int" && type!="float" && type!="bool")
                        fail("unsupported static component parameter type");
                }
                if (std::find(c.params.begin(),c.params.end(),param)!=c.params.end())
                    fail("duplicate parameter");
                c.params.push_back(param);
            } while (take(","));
            expect(")");
        }
        c.children=children();
        if (!p.components.emplace(name,std::move(c)).second) fail("duplicate component");
    }
public:
    Parser(std::string source,Project& project,bool fromLibrary=false):
        tokens(lex(source)),p(project),library(fromLibrary) {}
    void parse() {
        while (peek().value!="<eof>") {
            if (take(";")) continue;
            if (take("module")) {
                identifier();
                while (take(".")) identifier();
            } else if (take("import")) {
                if(library) fail("native web UI imports are not allowed inside library components");
                if(peek().quoted) {
                    if(literalString()!="noqeri/ui")
                        fail("use import \"noqeri/ui\" as ui for the native UI library");
                } else {
                    if(!take("noqeri"))fail("use import \"noqeri/ui\" as ui");
                    expect("/");expect("ui"); // deprecated legacy spelling
                }
                expect("as");
                if (!p.uiAlias.empty()) fail("duplicate noqeri/ui import");
                p.uiAlias=identifier();
            } else if (take("component")) component();
            else if (!library && (take("let")||take("const")||take("persist"))) {
                const auto declaration=tokens.at(pos-1).value;
                const auto name=identifier(); expect("=");
                const auto value=expression();
                if (!p.globals.emplace(name,evaluate(value,{},p.globals)).second)
                    fail("duplicate global name");
                if (declaration=="let") p.mutableState.insert(name);
                if (declaration=="persist") p.persistentState.insert(name);
            } else if (!library && take("database")) {
                if(p.databaseConfigured) fail("database may be declared only once");
                p.databaseConfigured=true;
                expect("{"); std::set<std::string> fields;
                while(!take("}")) {
                    const auto key=identifier();expect(":");
                    if(!fields.insert(key).second)fail("duplicate database configuration field");
                    const auto value=evaluate(expression(),{},p.globals);
                    if(key=="engine")p.database.engine=value;
                    else if(key=="path")p.database.path=value;
                    else if(key=="dsn_env")p.database.dsnEnvironment=value;
                    else if(key=="binary_env")p.database.binaryEnvironment=value;
                    else if(key=="key_env")p.database.keyEnvironment=value;
                    else fail("unknown database configuration field");
                    take(",");take(";");
                }
            } else if (!library && take("query")) {
                const auto name=identifier();expect("=");
                const auto sql=literalString();
                if(!p.queries.emplace(name,sql).second || p.globals.count(name))fail("duplicate query/global name");
            } else if (!library && take("theme")) {
                expect("{");
                while (!take("}")) {
                    const auto key=identifier(); expect(":");
                    const auto value=expression();
                    if (!p.theme.emplace(key,evaluate(value,{},p.globals)).second)
                        fail("duplicate theme token");
                    take(","); take(";");
                }
            } else if (!library && take("layout")) {
                if(p.hasLayout)fail("duplicate layout");
                p.hasLayout=true;p.layout=children();
            } else if (!library && take("route")) {
                const auto path=literalString();
                if (!p.routes.emplace(path,children()).second) fail("duplicate route");
            } else if (!library && take("view")) {
                if (p.hasFallback) fail("duplicate fallback view");
                p.fallback=children(); p.hasFallback=true;
            } else fail(library?"unsupported UI library declaration":
                                    "unsupported static Noqeri declaration (no JS, events, or loaders)");
            take(";");
        }
    }
};
std::string escaped(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (char c:in) {
        switch (c) {
            case '&': out+="&amp;"; break;
            case '<': out+="&lt;"; break;
            case '>': out+="&gt;"; break;
            case '"': out+="&quot;"; break;
            case '\'': out+="&#39;"; break;
            default: out+=c;
        }
    }
    return out;
}
bool validRoute(const std::string& route) {
    if(route.empty()||route[0]!='/'||route.find("//")!=std::string::npos||
       (route.size()>1&&route.back()=='/')||route.size()>256)return false;
    if(route=="/")return true;
    std::stringstream stream(route.substr(1));std::string segment;
    std::set<std::string> params;std::size_t segments=0;
    while(std::getline(stream,segment,'/')) {
        if(++segments>16||segment.empty()||segment.size()>64)return false;
        if(segment[0]==':') {
            const auto name=segment.substr(1);
            if(!validName(name)||!params.insert(name).second)return false;
        }else if(!std::all_of(segment.begin(),segment.end(),[](unsigned char c){
            return std::isalnum(c)||c=='_'||c=='-';
        }))return false;
    }
    return true;
}
bool dynamicRoute(const std::string& route) {
    return route.size()>1&&(route[1]==':'||route.find("/:")!=std::string::npos);
}

bool allowedTag(const std::string& tag) {
    static const std::set<std::string> tags={
        "a","abbr","article","aside","b","blockquote","br","button","caption","code",
        "dd","details","div","dl","dt","em","fieldset","figcaption","figure","footer",
        "form","h1","h2","h3","h4","h5","h6","header","hr","i","img","input",
        "label","li","main","nav","ol","option","p","picture","pre","section",
        "select","small","span","strong","summary","table","tbody","td","textarea",
        "th","thead","time","tr","ul"
    };
    return tags.count(tag)>0;
}
std::string safeAttr(const std::string& key) {
    static const std::set<std::string> attrs={
        "id","class","title","role","href","src","alt","type","name","value",
        "placeholder","for","disabled","checked","target","rel","tabindex",
        "action","method","required","minlength","maxlength","min","max","step","inputmode","autocomplete"
    };
    if (attrs.count(key)) return key;
    if (key.rfind("aria_",0)==0 || key.rfind("data_",0)==0) {
        for (std::size_t i=5;i<key.size();++i)
            if (!wordContinue(key[i])) throw std::runtime_error("invalid attribute name "+key);
        if (key.size()<=5) throw std::runtime_error("empty data or aria attribute");
        std::string result=key;
        std::replace(result.begin(),result.end(),'_','-');
        return result;
    }
    throw std::runtime_error("unsupported web-native attribute "+key);
}
bool safeUrl(const std::string& value) {
    const bool same=!value.empty()&&value[0]=='/'&&(value.size()<2||value[1]!='/');
    const bool https=value.rfind("https://",0)==0;
    const bool relative=value.rfind("./",0)==0&&! (value.rfind("./../",0)==0);
    const bool mail=value.rfind("mailto:",0)==0||value.rfind("tel:",0)==0;
    const bool anchor=value.rfind("#",0)==0;
    return (same||https||relative||mail||anchor) &&
        value.find('\\')==std::string::npos &&
        value.find('\r')==std::string::npos && value.find('\n')==std::string::npos;
}
std::string render(const std::vector<Node>& nodes,const Project& p,
                   const std::map<std::string,std::string>& locals={},int depth=0,
                   const std::vector<Node>* slotNodes=nullptr) {
    if (depth>32) throw std::runtime_error("component recursion limit exceeded");
    std::string result;
    for (const auto& n:nodes) {
        if(n.kind=="slot") {
            if(!slotNodes)throw std::runtime_error("layout slot used outside a layout");
            result+=render(*slotNodes,p,locals,depth+1,nullptr);continue;
        }
        if (n.kind=="text") {
            result+=escaped(evaluate(n.text,locals,p.globals));
            continue;
        }
        if (n.kind=="component") {
            std::string name=n.tag;
            if (name.find('.')!=std::string::npos) {
                const auto prefix=name.substr(0,name.find('.'));
                if (prefix!=p.uiAlias) throw std::runtime_error("undeclared component namespace "+prefix);
                name=name.substr(name.find('.')+1);
            }
            const auto component=p.components.find(name);
            if (component==p.components.end()) throw std::runtime_error("unknown component "+name);
            if (n.args.size()!=component->second.params.size())
                throw std::runtime_error("component argument count mismatch: "+name);
            std::map<std::string,std::string> arguments;
            for (std::size_t i=0;i<n.args.size();++i)
                arguments[component->second.params[i]]=evaluate(n.args[i],locals,p.globals);
            result+=render(component->second.children,p,arguments,depth+1,slotNodes);
            continue;
        }
        if (!allowedTag(n.tag)) throw std::runtime_error("unsafe or unsupported HTML element "+n.tag);
        if (!n.actionId.empty()) throw std::runtime_error("events require serve-app, not static build");
        result+="<"+n.tag;
        bool blankTarget=false,hasRel=false;
        for (const auto& [key,expr]:n.attrs) {
            const auto attr=safeAttr(key);
            const auto value=evaluate(expr,locals,p.globals);
            if (attr=="href"||attr=="src"||attr=="action") {
                if (!safeUrl(value)) throw std::runtime_error("unsafe URL in "+attr);
            }
            if (attr=="method"&&value!="get") throw std::runtime_error("static forms support GET only");
            if (attr=="target") {
                if (value!="_self"&&value!="_blank") throw std::runtime_error("unsupported target");
                blankTarget=value=="_blank";
            }
            if (attr=="rel") hasRel=true;
            if (attr=="disabled"||attr=="checked"||attr=="required") {
                if (value!="false"&&value!="0"&&value!="") result+=" "+attr;
            } else result+=" "+attr+"=\""+escaped(value)+"\"";
        }
        if (blankTarget&&!hasRel) result+=" rel=\"noopener noreferrer\"";
        result+=">";
        if (n.tag=="input"||n.tag=="img"||n.tag=="br"||n.tag=="hr") {
            if (!n.children.empty()) throw std::runtime_error("void HTML element cannot have children");
        } else {
            result+=render(n.children,p,locals,depth+1,slotNodes);
            result+="</"+n.tag+">";
        }
    }
    return result;
}
std::string css(const std::map<std::string,std::string>& theme) {
    std::map<std::string,std::string> tokens={
        {"primary","#3158d5"},{"accent","#15803d"},{"background","#f7f8fc"},
        {"surface","#ffffff"},{"text","#1b2436"},{"muted","#526177"},
        {"border","#d9e0eb"},{"radius","12"}
    };
    for (const auto& [key,value]:theme) {
        if (!tokens.count(key)) throw std::runtime_error("unknown web-native theme token: "+key);
        if (key=="radius") {
            if (value.empty()||value.size()>2||
                !std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';})||
                std::stoi(value)>32) throw std::runtime_error("theme radius must be 0..32");
        } else {
            if (value.size()!=4 && value.size()!=7) throw std::runtime_error("theme color must be hex");
            if (value[0]!='#'||!std::all_of(value.begin()+1,value.end(),[](char c){
                return std::isxdigit(static_cast<unsigned char>(c))!=0;
            })) throw std::runtime_error("theme color must be hex");
        }
        tokens[key]=value;
    }
    std::string out=":root{\n";
    for (const auto& [key,value]:tokens)
        out+="--nq-"+key+":"+value+(key=="radius"?"px":"")+";\n";
    out+="}\n";
    out+=R"CSS(*,*::before,*::after{box-sizing:border-box}
body{margin:0;color:var(--nq-text);background:var(--nq-background);font:16px/1.6 system-ui,sans-serif}
button,input{font:inherit}a{color:var(--nq-primary)}
:focus-visible{outline:3px solid var(--nq-accent);outline-offset:3px}
.nq-page{width:min(100% - 2rem,72rem);margin:2rem auto}
.nq-title{margin:.2rem 0 1rem;font-size:clamp(1.8rem,3vw,3rem);line-height:1.15}
.nq-lead{color:var(--nq-muted);font-size:1.12rem}
.nq-page-header{margin:0 0 2rem;max-width:56rem}
.nq-eyebrow{margin:0 0 .5rem;color:var(--nq-primary);font-size:.78rem;font-weight:750;text-transform:uppercase;letter-spacing:.13em}
.nq-section-header{margin:1.5rem 0 1rem}
.nq-section-title{margin:0 0 .45rem;font-size:clamp(1.15rem,2vw,1.55rem);line-height:1.3}
.nq-muted,.nq-field-help,.nq-footer{color:var(--nq-muted)}
.nq-field-help{font-size:.85rem}
.nq-field-error{color:#a32132;font-size:.9rem;font-weight:600}
.nq-panel{padding:1.5rem;border:1px solid var(--nq-border);border-radius:var(--nq-radius);background:var(--nq-surface)}
.nq-empty{margin:2rem 0;padding:2rem;border:1px dashed var(--nq-border);border-radius:var(--nq-radius);text-align:center}
.nq-footer{margin-top:3rem;padding:1.5rem 0;border-top:1px solid var(--nq-border);font-size:.85rem}
.nq-button-secondary{background:transparent;color:var(--nq-primary)}
.nq-stack > * + *{margin-top:1rem}
.nq-card{padding:1.5rem;background:var(--nq-surface);border:1px solid var(--nq-border);border-radius:var(--nq-radius);margin:1rem 0}
.nq-card-title{margin:0 0 .35rem;font-size:1.2rem}
.nq-button{display:inline-flex;align-items:center;justify-content:center;border:1px solid var(--nq-primary);border-radius:var(--nq-radius);padding:.65rem 1.1rem;background:var(--nq-primary);color:white;cursor:pointer;text-decoration:none;font-weight:600}
.nq-button:disabled{opacity:.5;cursor:not-allowed}
.nq-badge{display:inline-block;padding:.16rem .65rem;border-radius:999px;background:var(--nq-background);color:var(--nq-primary);border:1px solid var(--nq-border);font-size:.85rem}
.nq-alert{padding:1rem;border-left:4px solid var(--nq-accent);border-radius:var(--nq-radius);background:var(--nq-surface)}
.nq-navbar{display:flex;flex-wrap:wrap;align-items:center;gap:1rem;padding:.8rem 0;border-bottom:1px solid var(--nq-border)}
.nq-navbar a{font-weight:600;text-decoration:none}
.nq-field{display:grid;gap:.3rem;margin-bottom:1rem}.nq-field label{font-weight:600}
.nq-field input,.nq-field textarea{width:100%;min-height:2.6rem;padding:.5rem .75rem;border:1px solid var(--nq-border);border-radius:var(--nq-radius);color:var(--nq-text);background:var(--nq-surface)}
.nq-stat-value{font-size:2rem;font-weight:750;line-height:1.2}
.nq-stat-label{font-size:.9rem;color:var(--nq-muted)}
)CSS";
    return out;
}
fs::path uiSource(const char* argv0) {
    if (const char* env=std::getenv("NOQERI_WEB_UI_PATH")) return env;
    const fs::path executable=fs::absolute(argv0).parent_path();
    const std::vector<fs::path> locations={
        fs::current_path()/"Lib/web/ui.noqeri",
        executable/"../share/noqeri/Lib/web/ui.noqeri",
        executable/"../Lib/web/ui.noqeri",
#ifdef NOQERI_UI_SOURCE
        fs::path(NOQERI_UI_SOURCE),
#endif
    };
    for (const auto& path:locations) {
        std::error_code ec;
        if (fs::is_regular_file(path,ec)) return path;
    }
    throw std::runtime_error("cannot find Lib/web/ui.noqeri (set NOQERI_WEB_UI_PATH)");
}
void writeText(const fs::path& file,const std::string& content) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file,std::ios::binary|std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write output "+file.string());
    output << content;
    if (!output) throw std::runtime_error("failed writing "+file.string());
}
std::string htmlPage(const std::string& page,const std::string& cssPath,bool reactive=false) {
    const std::string scriptPolicy=reactive?"; script-src 'self'; connect-src 'self'":"";
    const std::string clientScript=reactive?
        "<script defer src=\"/__nq/reactive.js\"></script>":"";
    return "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
           "style-src 'self'; img-src 'self' data:"+scriptPolicy+"; form-action 'self'; base-uri 'none'\">"
           "<title>Noqeri native web</title><link rel=\"stylesheet\" href=\""+cssPath+
           "\">"+clientScript+"</head><body>"+page+"</body></html>\n";
}
// A single view is a complete Noqeri web application: treat it as the
// home route for native static output and server-rendered actions. Explicit
// routes keep their existing 404 fallback semantics unchanged.
void promoteStandaloneView(Project& project) {
    if(project.routes.empty()&&project.hasFallback) {
        project.routes.emplace("/",std::move(project.fallback));
        project.hasFallback=false;
    }
}
void build(const fs::path& input,const fs::path& output,const char* executable) {
    const fs::path root=fs::weakly_canonical(input);
    const fs::path dest=fs::absolute(output).lexically_normal();
    if (root==dest || dest.string().rfind(root.string()+std::string(1,fs::path::preferred_separator),0)==0)
        throw std::runtime_error("output must not be inside the source project");
    Project project;
    Parser(readText(root/"main.noqeri"),project).parse();
    promoteStandaloneView(project);
    if (project.routes.empty()) throw std::runtime_error("native web app requires at least one view or route");
    if(project.databaseConfigured||!project.queries.empty())
        throw std::runtime_error("database queries require serve-app, not static build");
    if (!project.uiAlias.empty()) Parser(readText(uiSource(executable)),project,true).parse();
    for (const auto& [route,_]:project.routes)
        if (!validRoute(route)) throw std::runtime_error("unsafe static route: "+route);
    for(const auto& [route,_]:project.routes)
        if(dynamicRoute(route))throw std::runtime_error("parameterized routes require serve-app, not static build");
    // Validate ALL routes and fallback before creating any output files.
    std::map<fs::path,std::string> pages;
    for (const auto& [route,nodes]:project.routes) {
        fs::path relative="index.html";
        int depth=0;
        if (route!="/") {
            std::stringstream ss(route.substr(1)); std::string segment; fs::path parts;
            while (std::getline(ss,segment,'/')) { parts/=segment; ++depth; }
            relative=parts/"index.html";
        }
        std::string cssPath;
        for(int d=0;d<depth;++d)cssPath+="../";
        cssPath+="theme.css";
        pages.emplace(relative,htmlPage(project.hasLayout?
            render(project.layout,project,{},0,&nodes):render(nodes,project),cssPath));
    }
    if (project.hasFallback) pages.emplace("404.html",htmlPage(project.hasLayout?
        render(project.layout,project,{},0,&project.fallback):render(project.fallback,project),"theme.css"));
    const auto styles=css(project.theme);
    fs::create_directories(dest);
    for (const auto& [name,html]:pages) writeText(dest/name,html);
    writeText(dest/"theme.css",styles);
    std::cout << "noqeri-web: rendered "<<project.routes.size()
              <<" static routes (no Node.js, JavaScript, or runtime dependencies) into "<<dest<<"\n";
}
#if !defined(_WIN32)
void sendAll(int fd,const std::string& data) {
    std::size_t offset=0;
    while (offset<data.size()) {
        const auto sent=::send(fd,data.data()+offset,data.size()-offset,0);
        if (sent<=0) return;
        offset+=static_cast<std::size_t>(sent);
    }
}
void respond(int fd,int status,const std::string& mime,const std::string& data,bool head) {
    const std::string reason=status==200?"OK":status==404?"Not Found":
                             status==405?"Method Not Allowed":"Bad Request";
    const std::string header="HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\n"
        "Content-Type: "+mime+"\r\nContent-Length: "+std::to_string(data.size())+
        "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self' data:; form-action 'self'; base-uri 'none'\r\n\r\n";
    sendAll(fd,header);
    if (!head) sendAll(fd,data);
}
void serve(const fs::path& input,int port) {
    if (port<1024||port>65535) throw std::runtime_error("port must be 1024..65535");
    const auto root=fs::canonical(input);
    const int server=::socket(AF_INET,SOCK_STREAM,0);
    if (server<0) throw std::runtime_error("cannot open HTTP listening socket");
    const int reuse=1;
    ::setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family=AF_INET; addr.sin_port=htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (::bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0 ||
        ::listen(server,16)!=0) {
        ::close(server);
        throw std::runtime_error("cannot bind localhost HTTP port");
    }
    ::signal(SIGPIPE,SIG_IGN);
    std::cout<<"noqeri-web: serving http://127.0.0.1:"<<port<<" (Ctrl+C to stop)\n"<<std::flush;
    for (;;) {
        const int client=::accept(server,nullptr,nullptr);
        if (client<0) continue;
        timeval timeout{}; timeout.tv_sec=3;
        ::setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        char request[8193];
        const auto length=::recv(client,request,8192,0);
        if (length<=0) { ::close(client); continue; }
        std::string line(request,static_cast<std::size_t>(length));
        const auto end=line.find("\r\n");
        const auto first=line.substr(0,end);
        std::istringstream parts(first);
        std::string method,path,protocol,extra;
        parts>>method>>path>>protocol>>extra;
        const bool head=method=="HEAD";
        if (method!="GET"&&!head) respond(client,405,"text/plain","Method Not Allowed",false);
        else if (!extra.empty()||(protocol!="HTTP/1.1"&&protocol!="HTTP/1.0")||
                 path.empty()||path[0]!='/'||path.find('%')!=std::string::npos||
                 path.find('\\')!=std::string::npos||path.find("//")!=std::string::npos||
                 path.find("..")!=std::string::npos||path.find('\0')!=std::string::npos) {
            respond(client,400,"text/plain","Bad Request",head);
        } else {
            path=path.substr(0,path.find('?'));
            if (path=="/") path="/index.html";
            else if (path.back()=='/') path+="index.html";
            else if (path.find('.')==std::string::npos) path+="/index.html";
            const fs::path filename=root/path.substr(1);
            std::error_code ec;
            const auto canonicalFile=fs::weakly_canonical(filename,ec);
            const auto relativeFile=canonicalFile.lexically_relative(root);
            const bool inside=!ec && !relativeFile.empty() &&
                *relativeFile.begin()!=".." && !relativeFile.is_absolute();
            if (inside && fs::is_regular_file(canonicalFile,ec) &&
                fs::file_size(canonicalFile,ec)<=1024*1024 &&
                (canonicalFile.extension()==".html"||canonicalFile.extension()==".css")) {
                const std::string mime=filename.extension()==".css"?"text/css; charset=utf-8":"text/html; charset=utf-8";
                respond(client,200,mime,readText(canonicalFile),head);
            } else {
                const auto fallback=root/"404.html";
                const auto data=fs::is_regular_file(fallback,ec)?readText(fallback):std::string("Not Found");
                respond(client,404,"text/html; charset=utf-8",data,head);
            }
        }
        ::close(client);
    }
}
#endif
#include "web_native_db.hpp"
#include "web_native_app.hpp"
#include "web_native_bundle.hpp"
} // namespace
#if !defined(NOQERI_WEB_BUNDLED)
int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="drivers") {
            const char* noesql=std::getenv("NOESQL_BIN");
            const char* provider=std::getenv("NOESQL_NATIVE_PROVIDER");
            std::cout<<"noesql: "<<(provider&&*provider?"session-v2 provider configured":
                (noesql&&*noesql?"native CLI configured":"requires NOESQL_NATIVE_PROVIDER or NOESQL_BIN"))
                <<" (NoeSQL owns the engine)\n";
#ifdef NOQERI_WEB_SQLITE
            std::cout<<"sqlite: compiled\n";
#else
            std::cout<<"sqlite: not compiled\n";
#endif
#ifdef NOQERI_WEB_POSTGRES
            std::cout<<"postgresql: compiled (requires live connection)\n";
#else
            std::cout<<"postgresql: not compiled\n";
#endif
#ifdef NOQERI_WEB_DUCKDB
            std::cout<<"duckdb: compiled\n";
#else
            std::cout<<"duckdb: not compiled\n";
#endif
#ifdef NOQERI_WEB_MYSQL
            std::cout<<"mysql: compiled (requires live connection)\n";
#else
            std::cout<<"mysql: not compiled\n";
#endif
#ifdef NOQERI_WEB_POSTGRES
            std::cout<<"neon: compiled via PostgreSQL/libpq (TLS required)\n";
#else
            std::cout<<"neon: not compiled\n";
#endif
#ifdef NOQERI_WEB_CURL
            std::cout<<"supabase: compiled (server-side HTTPS)\n";
            std::cout<<"firebase: compiled (Realtime Database HTTPS)\n";
#else
            std::cout<<"supabase: not compiled (libcurl required)\n";
            std::cout<<"firebase: not compiled (libcurl required)\n";
#endif
            return 0;
        }
        if(argc==4&&std::string(argv[1])=="compile") {
#if !defined(_WIN32)
            compileApplication(argv[2],argv[3],argv[0]);return 0;
#else
            throw std::runtime_error("native app compiler requires a POSIX host");
#endif
        }
        if (argc>=2 && std::string(argv[1])=="serve-app" && (argc==3||argc==4||argc==5)) {
#if !defined(_WIN32)
            int port=8080;
            if (argc>=4) {
                const std::string supplied=argv[3];
                if (supplied.empty()||!std::all_of(supplied.begin(),supplied.end(),
                    [](char c){return c>='0'&&c<='9';})) throw std::runtime_error("invalid HTTP port");
                port=std::stoi(supplied);
            }
            serveApp(argv[2],port,argc==5?fs::path(argv[4]):fs::path("noqeri-data/state.nqkv"),argv[0]);
            return 0;
#else
            throw std::runtime_error("native app server requires POSIX sockets");
#endif
        }
        if (argc>=2 && std::string(argv[1])=="build" && argc==4) {
            build(argv[2],argv[3],argv[0]); return 0;
        }
        if (argc>=2 && std::string(argv[1])=="serve" && (argc==3||argc==4)) {
#if !defined(_WIN32)
            int port=8080;
            if (argc==4) { const std::string supplied=argv[3];
                if (supplied.empty()||!std::all_of(supplied.begin(),supplied.end(),[](char c){return c>='0'&&c<='9';}))
                    throw std::runtime_error("invalid HTTP port");
                port=std::stoi(supplied);
            }
            serve(argv[2],port);
            return 0;
#else
            throw std::runtime_error("native HTTP serve is currently supported on POSIX platforms only");
#endif
        }
        std::cerr<<"Usage: noqeri-web build <project-dir> <output-dir>\n"
                 <<"       noqeri-web serve <output-dir> [port]  (localhost-only, POSIX)\n"
                 <<"       noqeri-web serve-app <source-dir> [port] [state-file]  (server actions, POSIX)\n"
                 <<"       noqeri-web drivers      show available external DB provider status\n"
                 <<"       noqeri-web compile <source-dir> <native-executable> (POSIX, bootstrap)\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr<<"noqeri-web: "<<e.what()<<"\n";
        return 1;
    }
}

#endif // NOQERI_WEB_BUNDLED
