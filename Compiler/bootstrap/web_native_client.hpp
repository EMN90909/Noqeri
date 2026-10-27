// SPDX-License-Identifier: GPL-3.0-only
// Native Noqeri -> browser ESM compiler (bounded CSR milestone).
// Emits client JavaScript from Noqeri declarations in C++; Node is not needed
// to build or run the output. Unsupported features fail closed.
#pragma once

std::string clientQuote(const std::string& source) {
    std::string result="\"";
    const char* hex="0123456789abcdef";
    for(unsigned char c:source) {
        if(c=='"'||c=='\\'){result+='\\';result+=static_cast<char>(c);}
        else if(c=='\n')result+="\\n";
        else if(c=='\r')result+="\\r";
        else if(c=='\t')result+="\\t";
        else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }
    return result+"\"";
}

std::string clientExpr(const Expr& expr,const Project& project,
                       const std::set<std::string>& local={}) {
    std::string result;
    for(const auto& atom:expr.atoms) {
        if(!result.empty())result+="+";
        if(!atom.variable) {
            result+=clientQuote(atom.value);
            continue;
        }
        const auto dot=atom.value.find('.');
        if(dot!=std::string::npos) {
            const auto root=atom.value.substr(0,dot);
            if(!project.clientLoads.count(root))
                throw std::runtime_error("unknown Noqeri client resource: "+root);
            std::string value="__nq_resources["+clientQuote(root)+"]";
            auto offset=dot+1;
            bool first=true;
            while(offset<atom.value.size()) {
                const auto next=atom.value.find('.',offset);
                const auto member=atom.value.substr(offset,next==std::string::npos?
                    std::string::npos:next-offset);
                if(!validName(member)||member=="__proto__"||
                   member=="prototype"||member=="constructor"||
                   (first&&member!="data"&&member!="loading"&&member!="error"))
                    throw std::runtime_error("unsafe Noqeri load resource member: "+member);
                value="__nq_member("+value+","+clientQuote(member)+")";
                first=false;
                if(next==std::string::npos)break;
                offset=next+1;
            }
            result+="String("+value+")";
        } else if(local.count(atom.value))
            result+="String(__nq_locals["+clientQuote(atom.value)+"])";
        else if(project.globals.count(atom.value))
            result+="String(__nq_state["+clientQuote(atom.value)+"])";
        else throw std::runtime_error("client view unknown variable: "+atom.value);
    }
    return result.empty()?"\"\"":result;
}

std::string clientNodes(const std::vector<Node>& nodes,const Project& project,
                        const std::set<std::string>& locals={},unsigned depth=0);

std::string clientEvents(const Node& node,const Project& project,
                         const std::set<std::string>& locals) {
    if(node.actionId.empty())return "{}";
    const bool click=node.eventName=="click"&&node.tag=="button";
    const bool submit=node.eventName=="submit"&&node.tag=="form";
    const bool input=(node.eventName=="input"||node.eventName=="change")&&
        (node.tag=="input"||node.tag=="textarea"||node.tag=="select");
    if(!click&&!submit&&!input)
        throw std::runtime_error("Noqeri client events require a matching button, form or input control");
    std::string code="{"+node.eventName+":(__nq_event)=>{";
    if(submit)code+="const __nq_fields=__nq_formFields(__nq_event);";
    for(const auto& mutation:node.mutations) {
        if(!project.mutableState.count(mutation.target) &&
           !project.persistentState.count(mutation.target))
            throw std::runtime_error("client action must assign a mutable or persisted state: "+mutation.target);
        const auto target="__nq_state["+clientQuote(mutation.target)+"]";
        if(mutation.op=="add") {
            if(mutation.value.atoms.size()!=2 ||
               mutation.value.atoms[1].variable)
                throw std::runtime_error("unsupported Noqeri client increment");
            code+=target+"=String(Number("+target+")+Number("+
                  clientQuote(mutation.value.atoms[1].value)+"));";
        } else if(mutation.op=="set")
            code+=target+"="+clientExpr(mutation.value,project,locals)+";";
        else if(mutation.op=="field") {
            if(submit) {
                code+="if(!Object.hasOwn(__nq_fields,"+clientQuote(mutation.field)+"))return;";
                code+=target+"=__nq_fields["+clientQuote(mutation.field)+"];";
            } else if(input && (mutation.field=="value"||mutation.field=="checked"))
                code+=target+"=String(__nq_inputEvent(__nq_event)."+mutation.field+");";
            else throw std::runtime_error("form field actions require a client submit or input value/checked");
        } else throw std::runtime_error(
            "native client actions support set/add/field only; databases and validators require serve-app");
        if(project.persistentState.count(mutation.target))
            code+="__nq_saveStored("+clientQuote(mutation.target)+","+target+");";
    }
    code+="__nq_schedule();}}";
    return code;
}

std::string clientNodes(const std::vector<Node>& nodes,const Project& project,
                        const std::set<std::string>& locals,unsigned depth) {
    if(depth>48)throw std::runtime_error("client element nesting limit exceeded");
    std::string out="[";
    bool first=true;
    for(const auto& node:nodes) {
        if(!first)out+=",";
        first=false;
        if(node.kind=="text") {
            out+=clientExpr(node.text,project,locals);
            continue;
        }
        if(node.kind=="component") {
            std::string name=node.tag;
            if(!project.uiAlias.empty() &&
               name.rfind(project.uiAlias+".",0)==0)
                name=name.substr(project.uiAlias.size()+1);
            if(!project.components.count(name))
                throw std::runtime_error("unknown client component: "+node.tag);
            // Client components expand as child arrays, preserving SSR DOM
            // without inserting an artificial wrapper element.
            out+="...__nq_component_"+name+"([";
            for(std::size_t i=0;i<node.args.size();++i) {
                if(i)out+=",";
                out+=clientExpr(node.args[i],project,locals);
            }
            out+="])";
            continue;
        }
        if(node.kind!="element"||!allowedTag(node.tag))
            throw std::runtime_error("unsupported native client element: "+node.kind);
        std::string attrs="{";
        bool firstAttr=true;
        for(const auto& attr:node.attrs) {
            const auto name=safeAttr(attr.first);
            if(!firstAttr)attrs+=",";
            firstAttr=false;
            attrs+=clientQuote(name)+":"+clientExpr(attr.second,project,locals);
        }
        attrs+="}";
        out+="__nq_h("+clientQuote(node.tag)+","+attrs+","+
             clientNodes(node.children,project,locals,depth+1)+","+
             clientEvents(node,project,locals)+")";
    }
    return out+"]";
}

void clientStripEvents(std::vector<Node>& nodes) {
    for(auto& node:nodes) {
        node.actionId.clear();
        node.eventName.clear();
        node.mutations.clear();
        clientStripEvents(node.children);
    }
}

fs::path clientRuntimePath(const char* executable) {
    if(const char* path=std::getenv("NOQERI_WEB_RUNTIME_PATH"))return path;
    const fs::path exe=fs::absolute(executable).parent_path();
    const std::vector<fs::path> choices={
        fs::current_path()/"Runtime/noqeri-web-runtime.mjs",
        exe/"../share/noqeri/runtime/noqeri-web-runtime.mjs",
        exe/"../Runtime/noqeri-web-runtime.mjs",
#ifdef NOQERI_WEB_RUNTIME_SOURCE
        fs::path(NOQERI_WEB_RUNTIME_SOURCE),
#endif
    };
    for(const auto& option:choices) {
        std::error_code ec;
        if(fs::is_regular_file(option,ec))return option;
    }
    throw std::runtime_error("missing client browser runtime (set NOQERI_WEB_RUNTIME_PATH)");
}

void buildClient(const fs::path& input,const fs::path& output,const char* executable) {
    const auto root=fs::canonical(input),out=fs::absolute(output).lexically_normal();
    const auto relative=out.lexically_relative(root);
    if(out==root||(!relative.empty()&&*relative.begin()!=".."&&!relative.is_absolute()))
        throw std::runtime_error("client output must be outside source tree");
    Project project;
    Parser(readText(root/"main.noqeri"),project).parse();
    if(!project.uiAlias.empty())
        Parser(readText(uiSource(executable)),project,true).parse();
    promoteStandaloneView(project);
    if(project.routes.empty())
        throw std::runtime_error("native client build requires a view or route");
    if(project.hasLayout||project.databaseConfigured||!project.queries.empty())
        throw std::runtime_error("client build does not support layout or database; use serve-app");
    for(const auto& [path,_]:project.routes)
        if(!validRoute(path)||dynamicRoute(path))
            throw std::runtime_error("native client build requires literal safe routes");

    std::string js="// Native Noqeri browser build (C++ bootstrap, no Node.js)\n";
    js+="import {element as __nq_h,createApp as __nq_createApp,createRouter as __nq_createRouter,readStored as __nq_readStored,saveStored as __nq_saveStored,safeMember as __nq_member,inputEvent as __nq_inputEvent,formFields as __nq_formFields} from './__noqeri/runtime.mjs';\n";
    js+="const __nq_state=Object.create(null);\n";
    for(const auto& [name,value]:project.globals)
        js+="__nq_state["+clientQuote(name)+"]=" +
          (project.persistentState.count(name)?
            "__nq_readStored("+clientQuote(name)+","+clientQuote(value)+")":
            clientQuote(value))+";\n";
    js+="const __nq_resources=Object.create(null);\n";
    for(const auto& [name,url]:project.clientLoads)
        js+="__nq_resources["+clientQuote(name)+"]={loading:true,data:null,error:null};\n";
    js+="let __nq_schedule=()=>{}, __nq_router=null;\n";
    for(const auto& [name,component]:project.components) {
        js+="function __nq_component_"+name+"(__nq_args){";
        js+="const __nq_locals=Object.create(null);";
        for(std::size_t i=0;i<component.params.size();++i)
            js+="__nq_locals["+clientQuote(component.params[i])+"]=__nq_args["+std::to_string(i)+"];";
        js+="return "+clientNodes(component.children,project,
                   std::set<std::string>(component.params.begin(),component.params.end()))+";}\n";
    }
    js+="function __nq_page(__nq_path){switch(__nq_path){";
    for(const auto& [path,nodes]:project.routes)
        js+="case "+clientQuote(path)+":return "+clientNodes(nodes,project)+";";
    js+="default:return "+(project.hasFallback?
              clientNodes(project.fallback,project):
              "[__nq_h('h1',{},['Page not found'],{})]")+";}}\n";
    js+="export function render(path=null){return __nq_h('div',{},__nq_page(path??(__nq_router?__nq_router.path():location.pathname)),{});}\n";
    js+="export function mount(root){if(!root)throw Error('Noqeri root missing');__nq_router=__nq_createRouter(root,()=>__nq_schedule());";
    js+="const app=__nq_createApp(root,render,{hydrate:Boolean(root.childNodes.length)});__nq_schedule=app.schedule;";
    for(const auto& [name,url]:project.clientLoads) {
        const auto slot="__nq_resources["+clientQuote(name)+"]";
        js+="app.load("+clientQuote(url)+",value=>{"+slot+
            "={loading:false,data:value,error:null};__nq_schedule();},error=>{"+
            slot+"={loading:false,data:null,error:String(error)};__nq_schedule();});";
    }
    js+="const dispose=app.dispose.bind(app);app.dispose=()=>{__nq_router.dispose();__nq_router=null;__nq_schedule=()=>{};dispose();};return app;}\n";
    js+="if(typeof document!=='undefined'){const root=document.getElementById('app');if(root)mount(root);}\n";

    Project htmlProject=project;
    for(auto& [_,nodes]:htmlProject.routes)clientStripEvents(nodes);
    for(auto& [_,component]:htmlProject.components)clientStripEvents(component.children);
    clientStripEvents(htmlProject.fallback);
    std::map<fs::path,std::string> pages;
    for(const auto& [route,nodes]:htmlProject.routes) {
        fs::path target="index.html";std::string prefix;
        if(route!="/"){
            target=fs::path(route.substr(1))/"index.html";
            prefix="../";
            for(char c:route)if(c=='/')prefix+="../";
            prefix.erase(prefix.size()-3);
        }
        // Async resources have no build-time data. Render an empty app root
        // instead of falsely prerendering unknown values; CSR mounts it.
        const auto body=project.clientLoads.empty()?render(nodes,htmlProject):std::string{};
        const std::string head="<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'\">"
            "<link rel=\"stylesheet\" href=\""+prefix+"theme.css\"><title>Noqeri client web</title></head><body>"
            "<main id=\"app\">"+(project.clientLoads.empty()?"<div>"+body+"</div>":std::string{})+
            "</main>"+(project.clientLoads.empty()?std::string{}:
                std::string("<noscript>Enable JavaScript to load this page's live data.</noscript>"))+
            "<script type=\"module\" src=\""+prefix+"main.mjs\"></script></body></html>\n";
        pages.emplace(target,head);
    }
    // Parse and render ALL routes before writing output: fail closed.
    const auto styles=css(htmlProject.theme);
    const auto runtime=readText(clientRuntimePath(executable));
    fs::create_directories(out/"__noqeri");
    for(const auto& [path,data]:pages)writeText(out/path,data);
    writeText(out/"theme.css",styles);
    writeText(out/"main.mjs",js);
    writeText(out/"__noqeri/runtime.mjs",runtime);
    std::cout<<"noqeri-web: compiled "<<pages.size()<<" native client routes without Node.js\n";
}
