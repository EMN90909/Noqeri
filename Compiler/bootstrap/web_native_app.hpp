// SPDX-License-Identifier: GPL-3.0-only
// Native server-driven Noqeri interactions. Included only by web_native.cpp.
// No JS runtime. Every mutation is an explicitly declared, bounded server action.
// Deliberately localhost-only; not a production HTTP stack.
#pragma once
#if !defined(_WIN32)

struct AppSession {
    std::map<std::string,std::string> values;
    std::string csrf;
    std::string principal;
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
        // Create mode-0600 data files with exclusive creation. A normal
        // ofstream creates world-readable files under permissive umasks.
        const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0)throw std::runtime_error("cannot securely create native state file");
        std::size_t offset=0;
        bool complete=true;
        while(offset<data.size()) {
            const auto written=::write(fd,data.data()+offset,data.size()-offset);
            if(written<=0) {complete=false;break;}
            offset+=static_cast<std::size_t>(written);
        }
        if(::fsync(fd)!=0)complete=false;
        if(::close(fd)!=0)complete=false;
        if(!complete)throw std::runtime_error("native data write or fsync failed");
        fs::rename(temporary,store.stateFile);
        // Sync the directory entry as well, so a successful response survives
        // an ordinary crash/power-loss boundary on supported local filesystems.
        const auto directory=parent.empty()?fs::path("."):parent;
        const int dir=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(dir<0)throw std::runtime_error("cannot open native data directory");
        const bool synced=::fsync(dir)==0;
        ::close(dir);
        if(!synced)throw std::runtime_error("cannot sync native data directory");
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
            std::size_t databaseCalls=0;
            for(const auto& mutation:n.mutations)if(mutation.op=="db")++databaseCalls;
            std::size_t mutatingCalls=0;
            for(const auto& mutation:n.mutations)if(mutation.op!="validate"&&mutation.op!="validate_email"&&
               mutation.op!="validate_number")++mutatingCalls;
            if(databaseCalls>0&&mutatingCalls!=1)
                throw std::runtime_error("database actions require one independently committed statement");
            if (!actions.emplace(n.actionId,&n).second)
                throw std::runtime_error("duplicate native action ID");
            for(const auto& mutation:n.mutations) {
                if(mutation.op=="validate"||mutation.op=="validate_email"||
                   mutation.op=="validate_number") {
                    if(n.eventName!="submit")throw std::runtime_error("field validation requires on.submit");
                    continue;
                }
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
                      const std::map<std::string,std::string>& locals={},int depth=0,bool inForm=false,
                      const std::vector<Node>* slotNodes=nullptr) {
    if (depth>32) throw std::runtime_error("component recursion limit");
    std::string result;
    for(const auto& n:nodes) {
        if(n.kind=="slot") {
            if(!slotNodes)throw std::runtime_error("slot outside layout");
            result+=renderApp(*slotNodes,p,ctx,locals,depth+1,inForm,nullptr);continue;
        }
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
            result+=renderApp(it->second.children,p,ctx,args,depth+1,inForm,slotNodes);
            continue;
        }
        if (!allowedTag(n.tag)) throw std::runtime_error("unsafe native element");
        if (n.tag=="form"&&inForm) throw std::runtime_error("nested forms");
        if (!n.actionId.empty() && inForm)
            throw std::runtime_error("native action cannot be nested inside another form");
        if (n.eventName=="click") {
            result+="<form method=\"post\" action=\""+escaped(ctx.path)+"\""+
                    (std::getenv("NOQERI_WEB_REACTIVE")&&
                     std::string(std::getenv("NOQERI_WEB_REACTIVE"))=="1"?
                     " data-nq-reactive=\"1\"":"")+">"+
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
            if(attr=="disabled"||attr=="checked"||attr=="required") {
                if(value!="false"&&value!="0"&&!value.empty())result+=" "+attr;
            }else result+=" "+attr+"=\""+escaped(value)+"\"";
        }
        if(blankTarget&&!hasRel)result+=" rel=\"noopener noreferrer\"";
        if(n.eventName=="submit") result+=" method=\"post\" action=\""+escaped(ctx.path)+"\""+
            (std::getenv("NOQERI_WEB_REACTIVE")&&
             std::string(std::getenv("NOQERI_WEB_REACTIVE"))=="1"?
             " data-nq-reactive=\"1\"":"");
        if(n.eventName=="click") result+=" type=\"submit\"";
        result+=">";
        if(n.eventName=="submit")result+=hiddenAction(n,ctx.csrf);
        if(n.tag=="input"||n.tag=="img"||n.tag=="br"||n.tag=="hr") {
            if(!n.children.empty())throw std::runtime_error("void element has children");
        }else{
            result+=renderApp(n.children,p,ctx,locals,depth+1,inForm||n.tag=="form",slotNodes);
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
    std::string method,path,host,origin,body,cookie,type,authorization;
    bool hasOrigin=false;
};
AppRequest receiveAppRequest(int fd) {
    // An absolute per-request deadline prevents trickling one byte before every
    // socket timeout (slowloris). Enforce it across both headers and body.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    timeval sendTimeout{};sendTimeout.tv_sec=4;
    ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&sendTimeout,sizeof(sendTimeout));
    std::string received;
    char buffer[4096];
    auto receiveChunk=[&](std::size_t maximum)->std::size_t {
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(
            deadline-std::chrono::steady_clock::now()).count();
        if(remaining<=0)throw std::runtime_error("HTTP request deadline exceeded");
        timeval timeout{};
        timeout.tv_sec=static_cast<decltype(timeout.tv_sec)>(remaining/1000000);
        timeout.tv_usec=static_cast<decltype(timeout.tv_usec)>(remaining%1000000);
        if(::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0)
            throw std::runtime_error("cannot set request timeout");
        const auto n=::recv(fd,buffer,std::min(sizeof(buffer),maximum),0);
        if(n<=0)throw std::runtime_error("incomplete or timed-out HTTP request");
        return static_cast<std::size_t>(n);
    };
    while (received.find("\r\n\r\n")==std::string::npos) {
        received.append(buffer,receiveChunk(4096));
        if(received.size()>8192)throw std::runtime_error("HTTP headers too large");
    }
    const auto at=received.find("\r\n\r\n");
    // Require CRLF framing, rejecting ambiguous bare-LF and lone-CR headers
    // that an upstream reverse proxy might interpret differently.
    for(std::size_t i=0;i<at+4;++i) {
        if(received[i]=='\r'&&(i+1>=at+4||received[i+1]!='\n'))
            throw std::runtime_error("invalid HTTP header line ending");
        if(received[i]=='\n'&&(i==0||received[i-1]!='\r'))
            throw std::runtime_error("invalid HTTP header line ending");
    }
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
    std::size_t headerCount=0;
    while(std::getline(head,line)) {
        if(++headerCount>64)throw std::runtime_error("too many HTTP headers");
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
            name=="content-length"||name=="transfer-encoding"||name=="authorization") &&
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
        if(name=="origin"){req.origin=val;req.hasOrigin=true;}
        if(name=="host")req.host=val;
        if(name=="authorization")req.authorization=val;
    }
    if(protocol=="HTTP/1.1"&&req.host.empty())
        throw std::runtime_error("missing HTTP Host header");
    if(req.method=="POST"&&(!hasLength||
        (req.type!="application/x-www-form-urlencoded"&&
         req.type!="application/x-www-form-urlencoded; charset=utf-8"&&
         req.type!="application/x-www-form-urlencoded;charset=utf-8")))
        throw std::runtime_error("unsupported POST encoding");
    if(req.method=="POST" && req.hasOrigin && req.origin.empty())
        throw std::runtime_error("empty Origin is not valid");
    // Older same-origin clients may omit Origin. Enable strict mode when the
    // application can guarantee standards-compliant browser-only POSTs.
    if(req.method=="POST" && !req.hasOrigin) {
        const char* strict=std::getenv("NOQERI_WEB_REQUIRE_ORIGIN");
        if(strict && std::string(strict)=="1")
            throw std::runtime_error("POST requires Origin in strict mode");
    }
    if(req.method!="POST"&&length!=0)throw std::runtime_error("unexpected request body");
    req.body=received.substr(at+4);
    if(req.body.size()>length)throw std::runtime_error("excess request body");
    while(req.body.size()<length) {
        req.body.append(buffer,receiveChunk(length-req.body.size()));
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
        status==401?"Unauthorized":status==403?"Forbidden":status==404?"Not Found":status==405?"Method Not Allowed":
        status==422?"Unprocessable Content":status==503?"Service Unavailable":"Internal Server Error";
    std::string header="HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\n"
        "Content-Type: "+mime+"\r\nContent-Length: "+std::to_string(data.size())+
        "\r\nCache-Control: "+(mime.rfind("text/css",0)==0 || mime.rfind("text/javascript",0)==0 ?
             "private, max-age=300, must-revalidate" : "no-store")+
        "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\nCross-Origin-Resource-Policy: same-origin\r\n"
        "Referrer-Policy: same-origin\r\nContent-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self' data:"+
        std::string(std::getenv("NOQERI_WEB_REACTIVE")&&
                    std::string(std::getenv("NOQERI_WEB_REACTIVE"))=="1"?
                    "; script-src 'self'; connect-src 'self'":"")+"; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n";
    if(status==401)header+=std::getenv("NOQERI_WEB_BEARER_TOKEN")?
        "WWW-Authenticate: Bearer realm=\"Noqeri native preview\"\r\n":
        "WWW-Authenticate: Basic realm=\"Noqeri native preview\", charset=\"UTF-8\"\r\n";
    if(!cookie.empty()) {
        const char* secure=std::getenv("NOQERI_WEB_SECURE_COOKIE");
        header+="Set-Cookie: _nq_sid="+cookie+"; Path=/; HttpOnly; SameSite=Strict";
        if((secure&&std::string(secure)=="1")||
           (std::getenv("NOQERI_WEB_PUBLIC_ORIGIN")!=nullptr))
            header+="; Secure";
        header+="\r\n";
    }
    if(!redirect.empty())header+="Location: "+redirect+"\r\n";
    sendAll(fd,header+"\r\n");
    if(!head)sendAll(fd,data);
    // No user-controlled path or request body enters structured access logs.
    std::cerr<<"{\"service\":\"noqeri-web\",\"event\":\"http_response\",\"status\":"<<status<<"}\n";
}
void sessionPrune(AppStore& app,std::chrono::seconds ttl=std::chrono::hours(1)) {
    const auto now=std::chrono::steady_clock::now();
    for(auto i=app.sessions.begin();i!=app.sessions.end();) {
        if(now-i->second.seen>ttl)i=app.sessions.erase(i);
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

struct RouteSelection {
    const std::vector<Node>* nodes=nullptr;
    std::map<std::string,std::string> params;
};
std::vector<std::string> splitRoute(const std::string& path) {
    std::vector<std::string> result;
    if(path=="/")return result;
    std::stringstream ss(path.substr(1));std::string item;
    while(std::getline(ss,item,'/'))result.push_back(item);
    return result;
}
bool routePatternsOverlap(const std::string& one,const std::string& two) {
    const auto a=splitRoute(one),b=splitRoute(two);
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        if(a[i]==b[i]||a[i][0]==':'||b[i][0]==':')continue;
        return false;
    }
    return true;
}
RouteSelection matchAppRoute(const Project& project,const std::string& path) {
    const auto direct=project.routes.find(path);
    if(direct!=project.routes.end())return {&direct->second,{}};
    const auto parts=splitRoute(path);
    for(const auto& [pattern,nodes]:project.routes) {
        if(!dynamicRoute(pattern))continue;
        const auto segments=splitRoute(pattern);
        if(parts.size()!=segments.size())continue;
        std::map<std::string,std::string> params;
        bool matches=true;
        for(std::size_t i=0;i<segments.size();++i) {
            if(segments[i][0]==':') {
                if(parts[i].empty()||parts[i].size()>64||
                   !std::all_of(parts[i].begin(),parts[i].end(),[](unsigned char ch){
                       return std::isalnum(ch)||ch=='_'||ch=='-';
                   })) {matches=false;break;}
                params.emplace(segments[i].substr(1),parts[i]);
            }else if(segments[i]!=parts[i]) {matches=false;break;}
        }
        if(matches)return {&nodes,std::move(params)};
    }
    return {};
}
std::size_t layoutSlotCount(const std::vector<Node>& nodes) {
    std::size_t count=0;
    for(const auto& n:nodes) {
        if(n.kind=="slot")++count;
        count+=layoutSlotCount(n.children);
    }
    return count;
}

bool constantTimeEquals(const std::string& a,const std::string& b) {
    const auto length=std::max(a.size(),b.size());
    unsigned mismatch=static_cast<unsigned>(a.size()^b.size());
    for(std::size_t i=0;i<length;++i) {
        const auto left=i<a.size()?static_cast<unsigned char>(a[i]):0;
        const auto right=i<b.size()?static_cast<unsigned char>(b[i]):0;
        mismatch|=left^right;
    }
    return mismatch==0;
}
std::string basicDecode(const std::string& input) {
    if(input.empty()||input.size()>684||input.size()%4!=0)return "";
    static const std::string chars=
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for(std::size_t i=0;i<input.size();i+=4) {
        unsigned values[4]={};
        bool padded=false;
        for(int k=0;k<4;++k) {
            const auto c=input[i+static_cast<std::size_t>(k)];
            if(c=='=') {
                if(i+4!=input.size()||k<2)return "";
                padded=true; values[k]=0;
            } else {
                if(padded)return "";
                const auto at=chars.find(c);
                if(at==std::string::npos)return "";
                values[k]=static_cast<unsigned>(at);
            }
        }
        const bool a=input[i+2]=='=',b=input[i+3]=='=';
        if(a&&!b)return "";
        if((a&&(values[1]&15))||(b&&!a&&(values[2]&3)))return "";
        result+=static_cast<char>((values[0]<<2)|(values[1]>>4));
        if(!a)result+=static_cast<char>((values[1]<<4)|(values[2]>>2));
        if(!b)result+=static_cast<char>((values[2]<<6)|values[3]);
    }
    return result;
}
bool nativeBasicAllowed(const std::string& auth,const std::string& expected) {
    if(expected.empty())return true;
    if(auth.rfind("Basic ",0)!=0)return false;
    return constantTimeEquals(basicDecode(auth.substr(6)),expected);
}
#include "web_native_authz.hpp"
// Opt-in progressive enhancement. No client-side framework or Node runtime;
// server-side validation and CSRF always remain authoritative.
std::string reactiveClientScript() {
    return R"NQJS("use strict";
// Native Noqeri SSR hydration: event delegation preserves existing server DOM.
// Navigation only fetches same-origin server HTML; actions remain CSRF checked.
(() => {
  const MAX_HTML = 1024 * 1024;
  let sequence = 0;
  let activeGet = null;
  const sameOrigin = (value) => {
    try {
      const url = new URL(value, location.href);
      return url.origin === location.origin && /^https?:$/.test(url.protocol)
        && !url.pathname.startsWith("/__nq/");
    } catch { return false; }
  };
  function syncAttributes(node, incoming) {
    for (const attribute of Array.from(node.attributes))
      if (!incoming.hasAttribute(attribute.name)) node.removeAttribute(attribute.name);
    for (const attribute of Array.from(incoming.attributes))
      if (node.getAttribute(attribute.name) !== attribute.value)
        node.setAttribute(attribute.name, attribute.value);
  }
  function reconcile(parent, incoming, depth = 0) {
    if (depth > 64) throw Error("Noqeri DOM depth limit exceeded");
    let oldNode = parent.firstChild;
    for (const next of Array.from(incoming.childNodes)) {
      if (!oldNode) {
        parent.appendChild(document.importNode(next, true));
        continue;
      }
      const following = oldNode.nextSibling;
      const compatible = oldNode.nodeType === next.nodeType
        && (oldNode.nodeType !== 1 || oldNode.localName === next.localName)
        && (oldNode.nodeType !== 1 || !oldNode.id || oldNode.id === next.id);
      if (!compatible) {
        parent.replaceChild(document.importNode(next, true), oldNode);
      } else if (oldNode.nodeType === 3 || oldNode.nodeType === 8) {
        if (oldNode.nodeValue !== next.nodeValue) oldNode.nodeValue = next.nodeValue;
      } else if (oldNode.nodeType === 1) {
        syncAttributes(oldNode, next);
        reconcile(oldNode, next, depth + 1);
      }
      oldNode = following;
    }
    while (oldNode) {
      const following = oldNode.nextSibling;
      parent.removeChild(oldNode);
      oldNode = following;
    }
  }
  async function show(response, mode, requestedUrl, ticket) {
    if (!response.ok || !sameOrigin(response.url)
      || !(response.headers.get("content-type") || "").toLowerCase().includes("text/html"))
      throw Error("Noqeri navigation rejected");
    const html = await response.text();
    if (html.length > MAX_HTML) throw Error("Noqeri HTML response too large");
    const incoming = new DOMParser().parseFromString(html, "text/html");
    if (!incoming.body || !incoming.querySelector('link[href="/theme.css"]'))
      throw Error("Not a native Noqeri document");
    if (ticket !== sequence) return;
    reconcile(document.body, incoming.body);
    if (incoming.title) document.title = incoming.title;
    const address = new URL(response.url);
    if (mode === "push" && address.pathname + address.search !== location.pathname + location.search)
      history.pushState(null, "", address.pathname + address.search);
    if (mode === "replace")
      history.replaceState(null, "", address.pathname + address.search);
    if (mode !== "replace" && mode !== "initial") window.scrollTo(0, 0);
    document.dispatchEvent(new CustomEvent("noqeri:updated"));
  }
  async function navigate(url, mode) {
    if (!sameOrigin(url)) return false;
    const ticket = ++sequence;
    activeGet?.abort();
    const controller = new AbortController();
    activeGet = controller;
    try {
      const response = await fetch(url, {
        method: "GET", credentials: "same-origin", redirect: "follow",
        headers: {Accept: "text/html"}, signal: controller.signal
      });
      await show(response, mode, url, ticket);
    } catch (error) {
      if (error.name !== "AbortError" && ticket === sequence) location.assign(url);
    } finally {
      if (activeGet === controller) activeGet = null;
    }
    return true;
  }
  document.addEventListener("click", (event) => {
    if (event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.metaKey
      || event.altKey || event.shiftKey) return;
    const link = event.target?.closest?.("a[href]");
    if (!link || link.hasAttribute("download") || (link.target && link.target !== "_self")) return;
    const href = link.getAttribute("href");
    if (!href || href.startsWith("#") || !sameOrigin(href)) return;
    const url = new URL(href, location.href);
    if (url.pathname === location.pathname && url.search === location.search && url.hash) return;
    event.preventDefault();
    void navigate(url.href, "push");
  });
  document.addEventListener("submit", async (event) => {
    const form = event.target;
    if (!(form instanceof HTMLFormElement) || form.dataset.nqReactive !== "1"
      || form.method.toLowerCase() !== "post" || !sameOrigin(form.action)) return;
    if (!form.reportValidity()) return;
    event.preventDefault();
    if (form.dataset.nqBusy === "1") return;
    form.dataset.nqBusy = "1";
    const ticket = ++sequence;
    activeGet?.abort();
    try {
      const data = new FormData(form);
      if (Array.from(data.values()).some(value => typeof value !== "string"))
        throw Error("File uploads are unsupported");
      const response = await fetch(form.action, {
        method: "POST", credentials: "same-origin", redirect: "follow",
        headers: {Accept: "text/html"},
        body: new URLSearchParams(data)
      });
      await show(response, "replace", form.action, ticket);
    } catch (_) {
      // Never repeat the POST automatically: the server may already have committed it.
      if (ticket === sequence) location.assign(form.action);
    } finally {
      delete form.dataset.nqBusy;
    }
  });
  window.addEventListener("popstate", () => { void navigate(location.href, "pop"); });
  document.documentElement.dataset.noqeriHydrated = "1";
})();
)NQJS";
}
#include "web_native_sessions.hpp"
void serveApp(const fs::path& input,int port,const fs::path& stateFile,const char* executable,
              const std::string* sourceOverride=nullptr,const std::string* uiOverride=nullptr) {
    if(port<1024||port>65535)throw std::runtime_error("port must be 1024..65535");
    const bool reactive=std::getenv("NOQERI_WEB_REACTIVE")&&
        std::string(std::getenv("NOQERI_WEB_REACTIVE"))=="1";
    const char* configuredUser=std::getenv("NOQERI_WEB_BASIC_USER");
    const char* configuredPassword=std::getenv("NOQERI_WEB_BASIC_PASSWORD");
    if(bool(configuredUser)!=bool(configuredPassword))
        throw std::runtime_error("both native Basic environment credentials are required");
    if(configuredUser&&(!*configuredUser||!*configuredPassword))
        throw std::runtime_error("Basic credentials may not be empty");
    const std::string basicExpected=configuredUser?
        std::string(configuredUser)+":"+configuredPassword:"";
    const char* configuredBearer=std::getenv("NOQERI_WEB_BEARER_TOKEN");
    const std::string bearerExpected=configuredBearer?configuredBearer:"";
    if(configuredBearer&&(bearerExpected.size()<32||bearerExpected.size()>512||
       bearerExpected.find_first_of("\r\n")!=std::string::npos))
        throw std::runtime_error("Bearer token must be 32..512 bytes without newlines");
    if(!basicExpected.empty()&&!bearerExpected.empty())
        throw std::runtime_error("configure either Basic or Bearer authentication, not both");
    const auto routePermissions=loadRoutePermissions();
    if(!routePermissions.empty()&&(!basicExpected.empty()||!bearerExpected.empty()))
        throw std::runtime_error("route bearer authorization cannot mix with global Basic/Bearer gates");
    // TLS terminates at a separately configured loopback reverse proxy. Never
    // trust X-Forwarded-* from arbitrary callers as an origin authority.
    std::string publicOrigin;
    if(const char* configured=std::getenv("NOQERI_WEB_PUBLIC_ORIGIN")) {
        publicOrigin=configured;
        constexpr const char* scheme="https://";
        if(publicOrigin.rfind(scheme,0)!=0||publicOrigin.size()<=8||
           publicOrigin.size()>256)
            throw std::runtime_error("public origin must be https://hostname[:port]");
        const auto authority=publicOrigin.substr(8);
        if(authority.front()=='.'||authority.back()=='.'||authority.front()=='-'||
           authority.find("..")!=std::string::npos||
           !std::all_of(authority.begin(),authority.end(),[](unsigned char ch){
               return std::isalnum(ch)||ch=='.'||ch=='-'||ch==':';
           }))
            throw std::runtime_error("invalid public HTTPS origin authority");
        if(authority.find(':')!=std::string::npos) {
            const auto portText=authority.substr(authority.find(':')+1);
            if(portText.empty()||portText.size()>5||
               !std::all_of(portText.begin(),portText.end(),[](char ch){return ch>='0'&&ch<='9';})||
               std::stoul(portText)==0||std::stoul(portText)>65535)
                throw std::runtime_error("invalid HTTPS origin port");
        }
    }
    // Explicit TTL supports short-lived sessions without changing the cookie
    // format; this is also used to test expiry without waiting an hour.
    std::chrono::seconds sessionTtl{3600};
    if(const char* setting=std::getenv("NOQERI_WEB_SESSION_TTL_SECONDS")) {
        const std::string value(setting);
        if(value.empty()||value.size()>4||
           !std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';}))
            throw std::runtime_error("session TTL must be 1..3600 seconds");
        const auto seconds=std::stoi(value);
        if(seconds<1||seconds>3600)
            throw std::runtime_error("session TTL must be 1..3600 seconds");
        sessionTtl=std::chrono::seconds(seconds);
    }
    AppStore app;
    app.stateFile=fs::absolute(stateFile).lexically_normal();
    Parser(sourceOverride?*sourceOverride:readText(input/"main.noqeri"),app.project).parse();
    if(!app.project.uiAlias.empty())
        Parser(uiOverride?*uiOverride:readText(uiSource(executable)),app.project,true).parse();
    promoteStandaloneView(app.project);
    if(app.project.routes.empty())throw std::runtime_error("serve-app requires a view or route");
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
        }else if(app.project.database.engine=="supabase"||
                 app.project.database.engine=="firebase") {
            if(statement.rfind("GET /",0)!=0||statement.size()>2048||
               statement.find_first_of("\r\n")!=std::string::npos)
                throw std::runtime_error("cloud view queries must use a single GET /path statement");
        }else {
            auto pos=statement.find_first_not_of(" \t\r\n");
            if(pos==std::string::npos||lower(statement.substr(pos,6))!="select")
                throw std::runtime_error("SQL view query must use SELECT");
        }
    }
    if(app.project.hasLayout) {
        if(layoutSlotCount(app.project.layout)!=1)
            throw std::runtime_error("native layout must contain exactly one slot {}");
        std::map<std::string,const Node*> found;
        validateActions(app.project.layout,app.project,found);
        if(!found.empty())throw std::runtime_error("actions in layout are unsupported");
    }
    for(const auto& [route,nodes]:app.project.routes) {
        if(!dynamicRoute(route))continue;
        for(const auto& [other,_]:app.project.routes) {
            if(other==route||!dynamicRoute(other))continue;
            if(routePatternsOverlap(route,other))
                throw std::runtime_error("ambiguous dynamic route patterns: "+route+" and "+other);
        }
        for(const auto& seg:splitRoute(route))if(!seg.empty()&&seg[0]==':') {
            const auto name=seg.substr(1);
            if(app.project.globals.count(name)||app.project.queries.count(name))
                throw std::runtime_error("route parameter conflicts with application state: "+name);
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
#if defined(NOQERI_WEB_SQLITE)
    std::unique_ptr<NativeSqliteSessions> sqliteSessions;
    if(const char* path=std::getenv("NOQERI_WEB_SESSION_SQLITE")) {
        if(!*path)throw std::runtime_error("SQLite session path may not be empty");
        if(!app.project.persistentState.empty())
            throw std::runtime_error("shared SQLite sessions cannot be mixed with process-local persist state");
        sqliteSessions=std::make_unique<NativeSqliteSessions>(fs::absolute(path));
    }
#else
    if(std::getenv("NOQERI_WEB_SESSION_SQLITE"))
        throw std::runtime_error("SQLite session backend requires NOQERI_WEB_SQLITE build option");
#endif
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
    std::mutex stateGuard;
    std::atomic<unsigned> activeClients{0};
    constexpr unsigned maxConcurrentClients=32;
    for (;;) {
        const int client=::accept(server,nullptr,nullptr);
        if(client<0)continue;
        if(activeClients.fetch_add(1)>=maxConcurrentClients) {
            appReply(client,503,"Server at capacity");
            ::close(client);
            activeClients.fetch_sub(1);
            continue;
        }
        std::thread([&,client] {
        try {
            const AppRequest req=receiveAppRequest(client);
            const auto routePath=req.path.substr(0,req.path.find('?'));
            const auto authorization=authorizeRoute(routePermissions,req.method,routePath,req.authorization);
            // Authenticate and validate the origin before acquiring the state lock.
            // GET rendering and independent database reads can now run concurrently.
            if(!bearerExpected.empty() ?
               !(req.authorization.rfind("Bearer ",0)==0&&
                 constantTimeEquals(req.authorization.substr(7),bearerExpected)) :
               !nativeBasicAllowed(req.authorization,basicExpected)) {
                appReply(client,401,"Authentication required");
            } else if(!authorization.allowed) {
                appReply(client,403,"Route access denied");
            } else {
            const auto allowedHostA="127.0.0.1:"+std::to_string(port);
            const auto allowedHostB="localhost:"+std::to_string(port);
            const auto allowedPublicHost=publicOrigin.empty()?std::string{}:publicOrigin.substr(8);
            if(!req.host.empty()&&req.host!=allowedHostA&&req.host!=allowedHostB&&
               (allowedPublicHost.empty()||req.host!=allowedPublicHost))
                throw std::runtime_error("Host is not a configured native origin");
            auto path=routePath;
            if(path.size()>1&&path.back()=='/')path.pop_back();
            const bool head=req.method=="HEAD";
            if(path=="/theme.css"&&req.method!="POST") {
                appReply(client,200,app.styles,"","",head,"text/css; charset=utf-8");
            } else if(reactive&&path=="/__nq/reactive.js"&&req.method!="POST") {
                appReply(client,200,reactiveClientScript(),"","",head,"text/javascript; charset=utf-8");
            } else {
                const auto page=matchAppRoute(app.project,path);
                if(!page.nodes) {
                    const auto content=app.project.hasFallback?
                        htmlPage(app.project.hasLayout?
                            render(app.project.layout,app.project,{},0,&app.project.fallback):
                            render(app.project.fallback,app.project),"/theme.css"):"Page not found";
                    appReply(client,404,content,"","",head);
                } else if(req.method=="POST") {
                    std::lock_guard<std::mutex> locked(stateGuard);
                    const auto sid=getSid(req.cookie);
#if defined(NOQERI_WEB_SQLITE)
                    std::unique_ptr<NativeSqliteSessions::Lease> sessionLease;
                    if(sqliteSessions) {
                        sessionLease=std::make_unique<NativeSqliteSessions::Lease>(*sqliteSessions);
                        AppSession loaded;
                        if(sqliteSessions->load(sid,loaded,sessionTtl))
                            app.sessions[sid]=std::move(loaded);
                        else app.sessions.erase(sid);
                    }
#endif
                    auto session=app.sessions.find(sid);
                    if(session==app.sessions.end() ||
                       session->second.principal!=authorization.principal ||
                       std::chrono::steady_clock::now()-session->second.seen>sessionTtl) {
                        appReply(client,403,"Session expired");
                    } else if(!publicOrigin.empty() ?
                              req.origin!=publicOrigin :
                              (!req.origin.empty()&&
                               req.origin!="http://127.0.0.1:"+std::to_string(port)&&
                               req.origin!="http://localhost:"+std::to_string(port))) {
                        appReply(client,403,"Invalid origin");
                    } else {
                        const auto fields=parseFormBody(req.body);
                        const auto action=fields.find("_nq_action");
                        const auto csrf=fields.find("_nq_csrf");
                        std::map<std::string,const Node*> actions;
                        validateActions(*page.nodes,app.project,actions);
                        const auto specified=action==fields.end()?actions.end():actions.find(action->second);
                        if(specified==actions.end()||csrf==fields.end()||!constantTimeEquals(csrf->second,session->second.csrf))
                            appReply(client,403,"Invalid native action");
                        else {
                            std::string validationError;
                            for(const auto& mutation:specified->second->mutations) {
                                if(mutation.op!="validate"&&mutation.op!="validate_email"&&
                                   mutation.op!="validate_number")continue;
                                const auto supplied=fields.find(mutation.field);
                                const auto length=supplied==fields.end()?0:supplied->second.size();
                                if(mutation.op=="validate_email") {
                                    const auto value=supplied==fields.end()?std::string{}:supplied->second;
                                    const auto at=value.find('@'), dot=value.rfind('.');
                                    const bool ascii=std::all_of(value.begin(),value.end(),[](unsigned char ch){
                                        return ch>32 && ch<127 && ch!='<' && ch!='>' && ch!='"' && ch!='\\';
                                    });
                                    if(value.size()>254||at==std::string::npos||at==0||
                                       dot==std::string::npos||dot<=at+1||dot+1>=value.size()||
                                       value.find('@',at+1)!=std::string::npos||!ascii) {
                                        validationError="Invalid email address for "+mutation.field;
                                        break;
                                    }
                                    continue;
                                }
                                if(mutation.op=="validate_number") {
                                    std::int64_t number=0;
                                    if(supplied==fields.end()||!parseDecimal(supplied->second,number)||
                                       number<static_cast<std::int64_t>(mutation.minLength)||
                                       number>static_cast<std::int64_t>(mutation.maxLength)) {
                                        validationError="Invalid number for "+mutation.field;
                                        break;
                                    }
                                    continue;
                                }
                                if(length<mutation.minLength||length>mutation.maxLength) {
                                    validationError="Invalid length for "+mutation.field+
                                        " (expected "+std::to_string(mutation.minLength)+
                                        ".."+std::to_string(mutation.maxLength)+" bytes)";
                                    break;
                                }
                            }
                            if(!validationError.empty()) {
                                const auto content=htmlPage(
                                    "<main class=\"nq-page\"><h1>Input validation failed</h1><p role=\"alert\">"+
                                    escaped(validationError)+"</p><a href=\""+escaped(path)+"\">Return</a></main>",
                                    "/theme.css");
                                appReply(client,422,content);
                            } else {
                            auto updated=currentValues(app,session->second);
                            for(const auto& [name,value]:page.params)updated[name]=value;
                            bool hasDb=false,hasPersist=false;
                            for(const auto& mutation:specified->second->mutations) {
                                if(mutation.op=="db")hasDb=true;
                                if(app.project.persistentState.count(mutation.target))hasPersist=true;
                            }
                            if(hasDb&&hasPersist)throw std::runtime_error("cannot atomically mix DB writes and .nqkv persist");
                            for(const auto& mutation:specified->second->mutations) {
                                if(mutation.op=="validate"||mutation.op=="validate_email"||
                                   mutation.op=="validate_number")continue;
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
                                    try {(void)executeDatabase(app.project.database,mutation.query,params);}
                                    catch(const std::exception&) {throw NativeDatabaseFailure{};}
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
#if defined(NOQERI_WEB_SQLITE)
                            if(sqliteSessions) {
                                sqliteSessions->save(sid,session->second);
                                sessionLease->commit();
                            }
#endif
                            appReply(client,303,"","",path);
                            }
                        }
                    }
                } else {
                    Project rendered;
                    std::string newCookie, sessionCsrf;
                    {
                    std::lock_guard<std::mutex> locked(stateGuard);
                    auto sid=getSid(req.cookie);
#if defined(NOQERI_WEB_SQLITE)
                    std::unique_ptr<NativeSqliteSessions::Lease> sessionLease;
                    if(sqliteSessions) {
                        sessionLease=std::make_unique<NativeSqliteSessions::Lease>(*sqliteSessions);
                        AppSession loaded;
                        if(sqliteSessions->load(sid,loaded,sessionTtl))
                            app.sessions[sid]=std::move(loaded);
                        else app.sessions.erase(sid);
                    }
#endif
                    auto session=app.sessions.find(sid);
                    if(session!=app.sessions.end() &&
                       std::chrono::steady_clock::now()-session->second.seen>sessionTtl) {
                        app.sessions.erase(session);
                        session=app.sessions.end();
                    }
                    if(session!=app.sessions.end() &&
                       session->second.principal!=authorization.principal)
                        session=app.sessions.end();
                    if(session==app.sessions.end()) {
                        sessionPrune(app,sessionTtl);
#if defined(NOQERI_WEB_SQLITE)
                        if(sqliteSessions)sqliteSessions->prune(sessionTtl);
#endif
                        sid=randomToken();
                        AppSession created;
                        created.csrf=randomToken();
                        created.principal=authorization.principal;
                        created.seen=std::chrono::steady_clock::now();
                        for(const auto& name:app.project.mutableState)
                            created.values[name]=app.project.globals.at(name);
                        session=app.sessions.emplace(sid,std::move(created)).first;
                        newCookie=sid;
                    }
                    session->second.seen=std::chrono::steady_clock::now();
#if defined(NOQERI_WEB_SQLITE)
                    if(sqliteSessions) {
                        sqliteSessions->save(sid,session->second);
                        sessionLease->commit();
                    }
#endif
                    rendered=app.project;
                    rendered.globals=currentValues(app,session->second);
                    for(const auto& [name,value]:page.params)rendered.globals[name]=value;
                    sessionCsrf=session->second.csrf;
                    } // release state lock before slow, read-only database queries and rendering
                    for(const auto& [name,statement]:rendered.queries)
                        try {rendered.globals[name]=executeDatabase(rendered.database,statement,{});}
                        catch(const std::exception&) {throw NativeDatabaseFailure{};}
                    const AppRenderContext context{path,sessionCsrf};
                    const auto body=rendered.hasLayout?
                        renderApp(rendered.layout,rendered,context,{},0,false,page.nodes):
                        renderApp(*page.nodes,rendered,context);
                    if(body.size()>1024*1024)throw std::runtime_error("generated HTML too large");
                    appReply(client,200,htmlPage(body,"/theme.css",reactive),newCookie,"",head);
                }
            }
            }
        } catch(const NativeDatabaseFailure&) {
            std::cerr<<"{\"service\":\"noqeri-web\",\"event\":\"database_failure\"}\n";
            appReply(client,503,"Database temporarily unavailable");
        } catch(const std::exception& e) {
            std::cerr<<"noqeri-web request error: "<<e.what()<<"\n";
            appReply(client,400,"Bad Request");
        }
        ::close(client);
        activeClients.fetch_sub(1);
        }).detach();
    }
}
#endif
