// Native server-driven Noqeri interactions. Included only by web_native.cpp.
// No JS runtime. Every mutation is an explicitly declared, bounded server action.
// Deliberately localhost-only; not a production HTTP stack.
#pragma once
#if !defined(_WIN32)

struct AppSession {
    std::map<std::string,std::string> values;
    std::string csrf;
    std::chrono::steady_clock::time_point seen;
};
struct AppStore {
    Project project;
    fs::path stateFile;
    std::map<std::string,std::string> persisted;
    std::map<std::string,AppSession> sessions;
    std::string styles;
};
std::string randomToken() {
    std::array<unsigned char,24> bytes{};
    std::ifstream source("/dev/urandom",std::ios::binary);
    if (!source.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
        throw std::runtime_error("secure system randomness unavailable");
    static const char digits[]="0123456789abcdef";
    std::string result;
    for (const auto b:bytes) { result+=digits[b>>4]; result+=digits[b&15]; }
    return result;
}
std::string hexEncode(const std::string& data) {
    static const char hex[]="0123456789abcdef";
    std::string result;
    for (unsigned char c:data) { result+=hex[c>>4];result+=hex[c&15]; }
    return result;
}
std::string hexDecode(const std::string& data) {
    if (data.size()%2 || data.size()>2048) throw std::runtime_error("corrupt native data length");
    std::string value;
    for (std::size_t i=0;i<data.size();i+=2) {
        const auto n=[](char c)->int {
            if (c>='0'&&c<='9') return c-'0';
            if (c>='a'&&c<='f') return c-'a'+10;
            throw std::runtime_error("corrupt native data hex");
        };
        value+=static_cast<char>((n(data[i])<<4)|n(data[i+1]));
    }
    return value;
}
void loadPersisted(AppStore& store) {
    for (const auto& name:store.project.persistentState)
        store.persisted[name]=store.project.globals.at(name);
    std::error_code ec;
    if (!fs::exists(store.stateFile,ec)) return;
    if (fs::is_symlink(store.stateFile,ec))
        throw std::runtime_error("refusing symlinked native data store");
    const auto data=readText(store.stateFile);
    std::istringstream in(data);std::string line;
    if (!std::getline(in,line) || line!="NQKV1")
        throw std::runtime_error("unsupported native data format");
    std::set<std::string> seen;
    while(std::getline(in,line)) {
        if (line.empty()) continue;
        const auto colon=line.find(':');
        if (colon==std::string::npos) throw std::runtime_error("corrupt native data record");
        const auto name=line.substr(0,colon);
        if (!store.project.persistentState.count(name) || !seen.insert(name).second)
            throw std::runtime_error("unrecognized or duplicate native data field");
        const auto value=hexDecode(line.substr(colon+1));
        if (value.size()>512) throw std::runtime_error("native data value too large");
        store.persisted[name]=value;
    }
}
void savePersisted(const AppStore& store,const std::map<std::string,std::string>& values) {
    if (store.project.persistentState.empty()) return;
    std::string data="NQKV1\n";
    for(const auto& [name,value]:values) {
        if (value.size()>512) throw std::runtime_error("native data value too large");
        data+=name+":"+hexEncode(value)+"\n";
    }
    if (data.size()>65536) throw std::runtime_error("native data store too large");
    const auto parent=store.stateFile.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
    const auto temporary=fs::path(store.stateFile.string()+"."+randomToken()+".tmp");
    try {
        writeText(temporary,data);
        fs::rename(temporary,store.stateFile);
    }catch(...) { std::error_code ec;fs::remove(temporary,ec);throw; }
}
// Validate and collect actions before accepting requests; never accept action names
// from clients unless declared on the exact requested route.
void validateActions(const std::vector<Node>& nodes,const Project& p,
                     std::map<std::string,const Node*>& actions,bool nestedForm=false) {
    for (const auto& n:nodes) {
        const bool form=n.tag=="form";
        if (form && nestedForm) throw std::runtime_error("nested forms are unsupported");
        if (!n.actionId.empty()) {
            if (!actions.emplace(n.actionId,&n).second)
                throw std::runtime_error("duplicate native action ID");
            for(const auto& mutation:n.mutations) {
                if(mutation.op=="db") {
                    if(!p.databaseConfigured)throw std::runtime_error("db.execute requires database declaration");
                    if(mutation.query.empty())throw std::runtime_error("empty native DB action");
                    for(const auto& param:mutation.dbParams) {
                        if(!validName(param.name))throw std::runtime_error("invalid database bind name");
                        if(param.value.atoms.size()==1&&param.value.atoms[0].variable&&
                           param.value.atoms[0].value.rfind("form.",0)==0&&n.eventName!="submit")
                            throw std::runtime_error("form values require on.submit");
                    }
                    continue;
                }
                if (!p.mutableState.count(mutation.target) &&
                    !p.persistentState.count(mutation.target))
                    throw std::runtime_error("action can only mutate let/persist variables: "+mutation.target);
                if (mutation.op=="field"&&n.eventName!="submit")
                    throw std::runtime_error("form fields require on.submit");
                if (mutation.op=="add" && mutation.value.atoms.size()!=2)
                    throw std::runtime_error("invalid numeric action");
            }
        }
        if (form && n.eventName=="submit") {
            for(const auto& [name,_]:n.attrs)
                if (name=="action"||name=="method") throw std::runtime_error("Noqeri owns native form action/method");
        }
        if (n.eventName=="click")
            for(const auto& [name,_]:n.attrs)
                if (name=="type"||name=="formaction"||name=="formmethod")
                    throw std::runtime_error("native click button type/action is framework-managed");
        if (!n.children.empty()) validateActions(n.children,p,actions,nestedForm||form);
    }
}
std::string hiddenAction(const Node& n,const std::string& csrf) {
    return "<input type=\"hidden\" name=\"_nq_action\" value=\""+escaped(n.actionId)+"\">"
           "<input type=\"hidden\" name=\"_nq_csrf\" value=\""+escaped(csrf)+"\">";
}
struct AppRenderContext {
    std::string path,csrf;
};
std::string renderApp(const std::vector<Node>& nodes,const Project& p,
                      const AppRenderContext& ctx,
                      const std::map<std::string,std::string>& locals={},int depth=0,bool inForm=false) {
    if (depth>32) throw std::runtime_error("component recursion limit");
    std::string result;
    for(const auto& n:nodes) {
        if (n.kind=="text") {
            result+=escaped(evaluate(n.text,locals,p.globals)); continue;
        }
        if (n.kind=="component") {
            std::string name=n.tag;
            const auto dot=name.find('.');
            if (dot!=std::string::npos) {
                if (name.substr(0,dot)!=p.uiAlias) throw std::runtime_error("undeclared UI namespace");
                name=name.substr(dot+1);
            }
            const auto it=p.components.find(name);
            if (it==p.components.end()||it->second.params.size()!=n.args.size())
                throw std::runtime_error("invalid component call: "+name);
            std::map<std::string,std::string> args;
            for(std::size_t i=0;i<n.args.size();++i)
                args[it->second.params[i]]=evaluate(n.args[i],locals,p.globals);
            result+=renderApp(it->second.children,p,ctx,args,depth+1,inForm);
            continue;
        }
        if (!allowedTag(n.tag)) throw std::runtime_error("unsafe native element");
        if (n.tag=="form"&&inForm) throw std::runtime_error("nested forms");
        if (!n.actionId.empty() && inForm)
            throw std::runtime_error("native action cannot be nested inside another form");
        if (n.eventName=="click") {
            result+="<form method=\"post\" action=\""+escaped(ctx.path)+"\">"+
                    hiddenAction(n,ctx.csrf);
        }
        result+="<"+n.tag;
        bool blankTarget=false,hasRel=false;
        for(const auto& [name,expr]:n.attrs) {
            const auto attr=safeAttr(name);
            const auto value=evaluate(expr,locals,p.globals);
            if ((attr=="href"||attr=="src"||attr=="action")&&!safeUrl(value))
                throw std::runtime_error("unsafe native URL attribute");
            if (attr=="method"&&value!="get") throw std::runtime_error("native forms only permit framework-owned POST");
            if (attr=="target") {
                if(value!="_self"&&value!="_blank") throw std::runtime_error("unsafe target");
                blankTarget=value=="_blank";
            }
            if(attr=="rel")hasRel=true;
            if(attr=="name"&&value.rfind("_nq_",0)==0)
                throw std::runtime_error("reserved native form field");
            if(attr=="disabled"||attr=="checked") {
                if(value!="false"&&value!="0"&&!value.empty())result+=" "+attr;
            }else result+=" "+attr+"=\""+escaped(value)+"\"";
        }
        if(blankTarget&&!hasRel)result+=" rel=\"noopener noreferrer\"";
        if(n.eventName=="submit") result+=" method=\"post\" action=\""+escaped(ctx.path)+"\"";
        if(n.eventName=="click") result+=" type=\"submit\"";
        result+=">";
        if(n.eventName=="submit")result+=hiddenAction(n,ctx.csrf);
        if(n.tag=="input"||n.tag=="img"||n.tag=="br"||n.tag=="hr") {
            if(!n.children.empty())throw std::runtime_error("void element has children");
        }else{
            result+=renderApp(n.children,p,ctx,locals,depth+1,inForm||n.tag=="form");
            result+="</"+n.tag+">";
        }
        if(n.eventName=="click")result+="</form>";
    }
    return result;
}
bool parseDecimal(const std::string& s,std::int64_t& value) {
    if(s.empty()||s.size()>18)return false;
    for(char c:s)if(c<'0'||c>'9')return false;
    try{value=std::stoll(s);return true;}catch(...){return false;}
}
std::string applyMutation(const Mutation& mutation,const std::map<std::string,std::string>& values,
                          const std::map<std::string,std::string>& fields) {
    if(mutation.op=="field") {
        const auto item=fields.find(mutation.field);
        if(item==fields.end())throw std::runtime_error("missing form field "+mutation.field);
        if(item->second.size()>256)throw std::runtime_error("form field too long");
        return item->second;
    }
    if(mutation.op=="add") {
        const auto old=values.find(mutation.target);
        if(old==values.end())throw std::runtime_error("undeclared state");
        std::int64_t current=0,delta=0;
        if(!parseDecimal(old->second,current)||!parseDecimal(mutation.value.atoms[1].value,delta) ||
           delta>1000000 || current>std::numeric_limits<std::int64_t>::max()-delta)
            throw std::runtime_error("unsafe integer increment");
        return std::to_string(current+delta);
    }
    return evaluate(mutation.value,{},values);
}
std::string urlDecode(const std::string& raw) {
    std::string result;
    for(std::size_t i=0;i<raw.size();++i) {
        if(raw[i]=='+') result+=' ';
        else if(raw[i]=='%') {
            if(i+2>=raw.size())throw std::runtime_error("incomplete form encoding");
            const auto decode=[](char c)->int {
                if(c>='0'&&c<='9')return c-'0';
                if(c>='A'&&c<='F')return c-'A'+10;
                if(c>='a'&&c<='f')return c-'a'+10;
                throw std::runtime_error("invalid URL encoding");
            };
            const unsigned char value=static_cast<unsigned char>((decode(raw[i+1])<<4)|decode(raw[i+2]));
            if(value==0||value==13||value==10) throw std::runtime_error("control character in form field");
            result+=static_cast<char>(value);i+=2;
        }else if(raw[i]=='\0'||raw[i]=='\r'||raw[i]=='\n')throw std::runtime_error("control character in form");
        else result+=raw[i];
    }
    return result;
}
std::map<std::string,std::string> parseFormBody(const std::string& data) {
    if(data.size()>4096) throw std::runtime_error("form body too large");
    std::map<std::string,std::string> result;
    std::istringstream in(data);std::string pair;
    while(std::getline(in,pair,'&')) {
        const auto eq=pair.find('=');
        if(eq==std::string::npos)throw std::runtime_error("invalid form field");
        auto name=urlDecode(pair.substr(0,eq));
        if(!validName(name)||name.size()>64)throw std::runtime_error("invalid form field name");
        auto value=urlDecode(pair.substr(eq+1));
        if(value.size()>512||!result.emplace(std::move(name),std::move(value)).second)
            throw std::runtime_error("duplicate or oversized form field");
        if(result.size()>32)throw std::runtime_error("too many form fields");
    }
    return result;
}
std::string lower(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return std::tolower(c);});
    return text;
}
struct AppRequest {
    std::string method,path,host,origin,body,cookie,type;
};
AppRequest receiveAppRequest(int fd) {
    timeval timeout{};timeout.tv_sec=4;
    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    std::string received;
    char buffer[4096];
    while (received.find("\r\n\r\n")==std::string::npos) {
        const auto n=::recv(fd,buffer,sizeof(buffer),0);
        if(n<=0)throw std::runtime_error("incomplete HTTP headers");
        received.append(buffer,static_cast<std::size_t>(n));
        if(received.size()>8192)throw std::runtime_error("HTTP headers too large");
    }
    const auto at=received.find("\r\n\r\n");
    std::istringstream head(received.substr(0,at));
    std::string first;std::getline(head,first);
    if(!first.empty()&&first.back()=='\r')first.pop_back();
    std::istringstream start(first);
    AppRequest req;std::string protocol,extra;
    start>>req.method>>req.path>>protocol>>extra;
    if(!extra.empty()||(protocol!="HTTP/1.0"&&protocol!="HTTP/1.1")||
       (req.method!="GET"&&req.method!="POST"&&req.method!="HEAD"))
        throw std::runtime_error("invalid HTTP request line");
    if(req.path.empty()||req.path[0]!='/'||req.path.find('%')!=std::string::npos||
       req.path.find("//")!=std::string::npos||req.path.find("..")!=std::string::npos||
       req.path.find('\\')!=std::string::npos)
        throw std::runtime_error("unsafe HTTP path");
    std::string line;std::size_t length=0;bool hasLength=false;
    std::set<std::string> singletonHeaders;
    while(std::getline(head,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        const auto split=line.find(':');
        if(split==std::string::npos)throw std::runtime_error("invalid HTTP header");
        auto name=lower(line.substr(0,split));
        if(name.empty()||!std::all_of(name.begin(),name.end(),[](unsigned char c){
            return std::isalnum(c)!=0||c=='-';
        }))throw std::runtime_error("invalid HTTP header name");
        std::string val=line.substr(split+1);
        if(std::any_of(val.begin(),val.end(),[](unsigned char c){
            return c==0||c==127||(c<32&&c!=9);
        }))throw std::runtime_error("control byte in HTTP header");
        if((name=="host"||name=="origin"||name=="cookie"||name=="content-type"||
            name=="content-length"||name=="transfer-encoding") &&
           !singletonHeaders.insert(name).second)
            throw std::runtime_error("duplicate HTTP routing or framing header");
        while(!val.empty()&&(val.front()==' '||val.front()=='\t'))val.erase(val.begin());
        if(name=="transfer-encoding"||name=="expect")throw std::runtime_error("unsupported HTTP transfer mode");
        if(name=="content-length") {
            if(hasLength||val.empty()||val.size()>5||
               !std::all_of(val.begin(),val.end(),[](char c){return c>='0'&&c<='9';}))
                throw std::runtime_error("invalid content-length");
            hasLength=true;length=static_cast<std::size_t>(std::stoul(val));
            if(length>4096)throw std::runtime_error("request too large");
        }
        if(name=="content-type")req.type=lower(val);
        if(name=="cookie")req.cookie=val;
        if(name=="origin")req.origin=val;
        if(name=="host")req.host=val;
    }
    if(protocol=="HTTP/1.1"&&req.host.empty())
        throw std::runtime_error("missing HTTP Host header");
    if(req.method=="POST"&&(!hasLength||
        (req.type!="application/x-www-form-urlencoded"&&
         req.type!="application/x-www-form-urlencoded; charset=utf-8")))
        throw std::runtime_error("unsupported POST encoding");
    if(req.method!="POST"&&length!=0)throw std::runtime_error("unexpected request body");
    req.body=received.substr(at+4);
    if(req.body.size()>length)throw std::runtime_error("excess request body");
    while(req.body.size()<length) {
        const auto n=::recv(fd,buffer,std::min(sizeof(buffer),length-req.body.size()),0);
        if(n<=0)throw std::runtime_error("incomplete HTTP body");
        req.body.append(buffer,static_cast<std::size_t>(n));
    }
    return req;
}
std::string getSid(const std::string& cookie) {
    std::istringstream in(cookie);std::string part;
    bool seen=false;
    while(std::getline(in,part,';')) {
        while(!part.empty()&&part.front()==' ')part.erase(part.begin());
        if(part.rfind("_nq_sid=",0)!=0)continue;
        if(seen)return ""; // duplicated authentication cookie is ambiguous
        seen=true;
        const auto sid=part.substr(8);
        if(sid.size()!=48 ||
           !std::all_of(sid.begin(),sid.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c))!=0;}))
            return "";
        return sid;
    }
    return "";
}
void appReply(int fd,int status,const std::string& data,const std::string& cookie="",
              const std::string& redirect="",bool head=false,const std::string& mime="text/html; charset=utf-8") {
    const auto reason=status==200?"OK":status==303?"See Other":status==400?"Bad Request":
        status==403?"Forbidden":status==404?"Not Found":status==405?"Method Not Allowed":"Internal Server Error";
    std::string header="HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\n"
        "Content-Type: "+mime+"\r\nContent-Length: "+std::to_string(data.size())+
        "\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: same-origin\r\nContent-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self' data:; form-action 'self'; base-uri 'none'\r\n";
    if(!cookie.empty())header+="Set-Cookie: _nq_sid="+cookie+"; Path=/; HttpOnly; SameSite=Strict\r\n";
    if(!redirect.empty())header+="Location: "+redirect+"\r\n";
    sendAll(fd,header+"\r\n");
    if(!head)sendAll(fd,data);
}
void sessionPrune(AppStore& app) {
    const auto now=std::chrono::steady_clock::now();
    for(auto i=app.sessions.begin();i!=app.sessions.end();) {
        if(now-i->second.seen>std::chrono::hours(1))i=app.sessions.erase(i);
        else ++i;
    }
    while(app.sessions.size()>=256) {
        auto oldest=app.sessions.begin();
        for(auto it=app.sessions.begin();it!=app.sessions.end();++it)
            if(it->second.seen<oldest->second.seen)oldest=it;
        app.sessions.erase(oldest);
    }
}
std::map<std::string,std::string> currentValues(const AppStore& app,const AppSession& session) {
    auto result=app.project.globals;
    for(const auto& [k,v]:session.values)result[k]=v;
    for(const auto& [k,v]:app.persisted)result[k]=v;
    return result;
}
void serveApp(const fs::path& input,int port,const fs::path& stateFile,const char* executable) {
    if(port<1024||port>65535)throw std::runtime_error("port must be 1024..65535");
    AppStore app;
    app.stateFile=fs::absolute(stateFile).lexically_normal();
    Parser(readText(input/"main.noqeri"),app.project).parse();
    if(!app.project.uiAlias.empty())
        Parser(readText(uiSource(executable)),app.project,true).parse();
    if(app.project.routes.empty())throw std::runtime_error("serve-app requires routes");
    if(app.project.databaseConfigured)verifyDbConfig(app.project.database);
    if(!app.project.queries.empty()&&!app.project.databaseConfigured)
        throw std::runtime_error("queries require database declaration");
    for(const auto& [name,statement]:app.project.queries) {
        if(app.project.globals.count(name))throw std::runtime_error("query name conflicts with state: "+name);
        if(statement.empty())throw std::runtime_error("empty query");
        // These values are injected into escaped HTML and may never be used for writes.
        if(app.project.database.engine=="noesql") {
            const auto split=statement.find(' ');
            if(split==std::string::npos || !validName(statement.substr(0,split)) ||
               statement.substr(split+1,4)!="get " ||
               statement.find_first_of("\r\n;{}")!=std::string::npos)
                throw std::runtime_error("NoeSQL view query must be a single table get statement");
        }else {
            auto pos=statement.find_first_not_of(" \t\r\n");
            if(pos==std::string::npos||lower(statement.substr(pos,6))!="select")
                throw std::runtime_error("SQL view query must use SELECT");
        }
    }
    app.styles=css(app.project.theme);
    for(const auto& [name,_]:app.project.components) {
        std::map<std::string,const Node*> found;
        validateActions(app.project.components.at(name).children,app.project,found);
        if(!found.empty())throw std::runtime_error("actions in reusable components are not supported yet");
    }
    for(const auto& [route,nodes]:app.project.routes) {
        if(!validRoute(route))throw std::runtime_error("unsafe route path");
        std::map<std::string,const Node*> found;
        validateActions(nodes,app.project,found);
    }
    loadPersisted(app);
    const int server=::socket(AF_INET,SOCK_STREAM,0);
    if(server<0)throw std::runtime_error("cannot create native app socket");
    int reuse=1;::setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    sockaddr_in addr{};addr.sin_family=AF_INET;
    addr.sin_port=htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(::bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0||
       ::listen(server,16)!=0) {
        ::close(server);throw std::runtime_error("cannot bind native app port");
    }
    ::signal(SIGPIPE,SIG_IGN);
    std::cout<<"noqeri-web: server-rendered app at http://127.0.0.1:"<<port
             <<" (no Node or JavaScript; local development only)\n"<<std::flush;
    for (;;) {
        const int client=::accept(server,nullptr,nullptr);
        if(client<0)continue;
        try {
            const AppRequest req=receiveAppRequest(client);
            const auto allowedHostA="127.0.0.1:"+std::to_string(port);
            const auto allowedHostB="localhost:"+std::to_string(port);
            if(!req.host.empty()&&req.host!=allowedHostA&&req.host!=allowedHostB)
                throw std::runtime_error("Host is not the native loopback origin");
            auto path=req.path.substr(0,req.path.find('?'));
            if(path.size()>1&&path.back()=='/')path.pop_back();
            const bool head=req.method=="HEAD";
            if(path=="/theme.css"&&req.method!="POST") {
                appReply(client,200,app.styles,"","",head,"text/css; charset=utf-8");
            } else {
                const auto page=app.project.routes.find(path);
                if(page==app.project.routes.end()) {
                    const auto content=app.project.hasFallback?
                        htmlPage(render(app.project.fallback,app.project),"theme.css"):"Page not found";
                    appReply(client,404,content,"","",head);
                } else if(req.method=="POST") {
                    auto session=app.sessions.find(getSid(req.cookie));
                    if(session==app.sessions.end()) {
                        appReply(client,403,"Session expired");
                    } else if(!req.origin.empty()&&req.origin!="http://127.0.0.1:"+std::to_string(port)&&
                              req.origin!="http://localhost:"+std::to_string(port)) {
                        appReply(client,403,"Invalid origin");
                    } else {
                        const auto fields=parseFormBody(req.body);
                        const auto action=fields.find("_nq_action");
                        const auto csrf=fields.find("_nq_csrf");
                        std::map<std::string,const Node*> actions;
                        validateActions(page->second,app.project,actions);
                        const auto specified=action==fields.end()?actions.end():actions.find(action->second);
                        if(specified==actions.end()||csrf==fields.end()||csrf->second!=session->second.csrf)
                            appReply(client,403,"Invalid native action");
                        else {
                            auto updated=currentValues(app,session->second);
                            bool hasDb=false,hasPersist=false;
                            for(const auto& mutation:specified->second->mutations) {
                                if(mutation.op=="db")hasDb=true;
                                if(app.project.persistentState.count(mutation.target))hasPersist=true;
                            }
                            if(hasDb&&hasPersist)throw std::runtime_error("cannot atomically mix DB writes and .nqkv persist");
                            for(const auto& mutation:specified->second->mutations) {
                                if(mutation.op=="db") {
                                    std::vector<DbParameter> params;
                                    for(const auto& param:mutation.dbParams) {
                                        std::string value;
                                        if(param.value.atoms.size()==1&&param.value.atoms[0].variable&&
                                           param.value.atoms[0].value.rfind("form.",0)==0) {
                                            const auto field=param.value.atoms[0].value.substr(5);
                                            const auto data=fields.find(field);
                                            if(data==fields.end())throw std::runtime_error("missing bound form field");
                                            value=data->second;
                                        }else value=evaluate(param.value,{},updated);
                                        if(value.size()>512)throw std::runtime_error("database binding too long");
                                        params.push_back({param.name,std::move(value)});
                                    }
                                    (void)executeDatabase(app.project.database,mutation.query,params);
                                }else {
                                    auto value=applyMutation(mutation,updated,fields);
                                    if(value.size()>512)throw std::runtime_error("action result too long");
                                    updated[mutation.target]=std::move(value);
                                }
                            }
                            auto nextPersist=app.persisted;
                            for(const auto& name:app.project.persistentState)
                                nextPersist[name]=updated.at(name);
                            if(nextPersist!=app.persisted)savePersisted(app,nextPersist);
                            app.persisted=std::move(nextPersist);
                            for(const auto& name:app.project.mutableState)
                                session->second.values[name]=updated.at(name);
                            session->second.csrf=randomToken(); // invalidate replayed form posts
                            session->second.seen=std::chrono::steady_clock::now();
                            appReply(client,303,"","",path);
                        }
                    }
                } else {
                    auto sid=getSid(req.cookie);
                    auto session=app.sessions.find(sid);
                    std::string newCookie;
                    if(session==app.sessions.end()) {
                        sessionPrune(app);
                        sid=randomToken();
                        AppSession created;
                        created.csrf=randomToken();
                        created.seen=std::chrono::steady_clock::now();
                        for(const auto& name:app.project.mutableState)
                            created.values[name]=app.project.globals.at(name);
                        session=app.sessions.emplace(sid,std::move(created)).first;
                        newCookie=sid;
                    }
                    session->second.seen=std::chrono::steady_clock::now();
                    Project rendered=app.project;
                    rendered.globals=currentValues(app,session->second);
                    for(const auto& [name,statement]:app.project.queries)
                        rendered.globals[name]=executeDatabase(app.project.database,statement,{});
                    const AppRenderContext context{path,session->second.csrf};
                    const auto body=renderApp(page->second,rendered,context);
                    if(body.size()>1024*1024)throw std::runtime_error("generated HTML too large");
                    appReply(client,200,htmlPage(body,"/theme.css"),newCookie,"",head);
                }
            }
        } catch(const std::exception& e) {
            std::cerr<<"noqeri-web request error: "<<e.what()<<"\n";
            appReply(client,400,"Bad Request");
        }
        ::close(client);
    }
}
#endif
