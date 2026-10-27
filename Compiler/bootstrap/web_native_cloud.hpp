// SPDX-License-Identifier: GPL-3.0-only
// Bounded HTTPS cloud Data API boundary. The source request is authored in
// .noqeri; user fields are JSON values only, never URL or query fragments.
#pragma once
#ifdef NOQERI_WEB_CLOUD_CURL
#include <curl/curl.h>
#endif

struct CloudPlan {std::string method,resource,body;};
CloudPlan prepareCloudRequest(const std::string& engine,const std::string& source,
                              const std::vector<DbParameter>& params) {
    if(engine!="supabase"&&engine!="firebase")
        throw std::runtime_error("unsupported cloud engine");
    const auto blank=source.find(' ');
    if(source.empty()||source.size()>1024||blank==std::string::npos||
       source.find(' ',blank+1)!=std::string::npos)
        throw std::runtime_error("cloud statement must be METHOD resource");
    CloudPlan plan{source.substr(0,blank),source.substr(blank+1),{}};
    if(plan.method!="GET"&&plan.method!="POST"&&plan.method!="PUT"&&
       plan.method!="PATCH"&&plan.method!="DELETE")
        throw std::runtime_error("unsupported cloud HTTP method");
    if(plan.resource.empty()||plan.resource[0]=='/'||plan.resource.find("..")!=std::string::npos||
       plan.resource.find("//")!=std::string::npos||plan.resource.find('%')!=std::string::npos||
       plan.resource.find('#')!=std::string::npos)
        throw std::runtime_error("invalid cloud resource");
    for(unsigned char c:plan.resource)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
             c=='_'||c=='-'||c=='/'||c=='.'||c=='?'||c=='='||c=='&'||
             c==','||c=='*'||c=='('||c==')'||c=='!'))
            throw std::runtime_error("invalid cloud resource character");
    if(engine=="supabase") {
        if(plan.resource.find('/')!=std::string::npos)
            throw std::runtime_error("Supabase resource must be one table or view");
        if(!std::isalpha(static_cast<unsigned char>(plan.resource[0]))&&
           plan.resource[0]!='_')throw std::runtime_error("invalid Supabase table name");
    }else{
        if(plan.resource.find('?')!=std::string::npos)
            throw std::runtime_error("Firebase queries are not supported in this adapter");
        if(plan.resource.size()>=5&&plan.resource.substr(plan.resource.size()-5)==".json")
            throw std::runtime_error("Noqeri adds the Firebase .json suffix");
    }
    if((plan.method=="GET"||plan.method=="DELETE")&&!params.empty())
        throw std::runtime_error("cloud GET and DELETE cannot bind body fields");
    if(plan.method=="POST"||plan.method=="PUT"||plan.method=="PATCH") {
        if(params.empty()||params.size()>32)
            throw std::runtime_error("cloud write needs 1..32 body fields");
        std::set<std::string> names;
        plan.body="{";
        for(const auto& p:params) {
            if(!validName(p.name)||!names.insert(p.name).second||p.value.size()>4096)
                throw std::runtime_error("invalid cloud body field");
            if(plan.body!="{")plan.body+=",";
            plan.body+=dbJsonQuote(p.name)+":"+dbJsonQuote(p.value);
        }
        plan.body+="}";
        if(plan.body.size()>16384)throw std::runtime_error("cloud JSON exceeds limit");
    }
    return plan;
}
std::string cloudOrigin(const std::string& engine,const std::string& configured) {
    if(configured.rfind("https://",0)!=0||configured.size()>256)
        throw std::runtime_error("cloud endpoint must use HTTPS");
    auto hostname=configured.substr(8);
    if(!hostname.empty()&&hostname.back()=='/')hostname.pop_back();
    if(hostname.empty()||hostname.front()=='.'||hostname.front()=='-'||
       hostname.find("..")!=std::string::npos ||
       !std::all_of(hostname.begin(),hostname.end(),[](unsigned char c){
           return std::isalnum(c)||c=='.'||c=='-';
       }))throw std::runtime_error("cloud endpoint must be a bare DNS hostname");
    const auto suffix=[&](const std::string& tail) {
        return hostname.size()>tail.size()&&
               hostname.compare(hostname.size()-tail.size(),tail.size(),tail)==0;
    };
    if(engine=="supabase") {
        if(!suffix(".supabase.co")&&!suffix(".supabase.in"))
            throw std::runtime_error("Supabase endpoint must be an official project hostname");
    }else if(engine=="firebase") {
        if(!suffix(".firebaseio.com")&&!suffix(".firebasedatabase.app"))
            throw std::runtime_error("Firebase endpoint must be an RTDB hostname");
    }else throw std::runtime_error("unsupported cloud endpoint engine");
    return "https://"+hostname;
}
#ifdef NOQERI_WEB_CLOUD_CURL
std::size_t cloudResponseWrite(char* ptr,std::size_t width,std::size_t count,void* user) {
    if(width!=0&&count>65536/width)return 0;
    auto* body=static_cast<std::string*>(user);
    const auto size=width*count;
    if(body->size()+size>65536)return 0;
    body->append(ptr,size);
    return size;
}
std::string queryCloud(const DbConfiguration& db,const std::string& statement,
                       const std::vector<DbParameter>& params) {
    const auto plan=prepareCloudRequest(db.engine,statement,params);
    const char* endpoint=std::getenv(db.endpointEnvironment.c_str());
    if(!endpoint||!*endpoint)throw std::runtime_error("cloud endpoint environment not set");
    const auto origin=cloudOrigin(db.engine,endpoint);
    const char* key=std::getenv(db.keyEnvironment.c_str());
    const char* token=db.tokenEnvironment.empty()?nullptr:
        std::getenv(db.tokenEnvironment.c_str());
    if(db.engine=="supabase"&&(!key||!*key))
        throw std::runtime_error("Supabase publishable API key required");
    if(db.engine=="firebase"&&(!token||!*token))
        throw std::runtime_error("Firebase server-side OAuth2 token required");
    for(const auto* secret:{key,token})if(secret&&
        (std::strlen(secret)>4096||std::strpbrk(secret,"\r\n")!=nullptr))
        throw std::runtime_error("invalid cloud secret");
    static const bool ready=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    if(!ready)throw std::runtime_error("HTTPS adapter initialization failed");
    CURL* handle=curl_easy_init();
    if(!handle)throw std::runtime_error("HTTPS allocation failed");
    struct Dispose {CURL* h;~Dispose(){curl_easy_cleanup(h);}} dispose{handle};
    curl_slist* headers=nullptr;
    struct DisposeHeaders {curl_slist*& h;~DisposeHeaders(){curl_slist_free_all(h);}} disposeHeaders{headers};
    auto header=[&](const std::string& value) {
        auto* next=curl_slist_append(headers,value.c_str());
        if(!next)throw std::runtime_error("HTTPS header allocation failed");
        headers=next;
    };
    header("Accept: application/json");
    header("Content-Type: application/json");
    if(db.engine=="supabase") {
        header(std::string("apikey: ")+key);
        // Authorization is optional. Never assume a publishable key is JWT.
        if(token&&*token)header(std::string("Authorization: Bearer ")+token);
    } else header(std::string("Authorization: Bearer ")+token);
    std::string result;
    const auto url=origin+(db.engine=="supabase"?"/rest/v1/":"/")+
                    plan.resource+(db.engine=="firebase"?".json":"");
    curl_easy_setopt(handle,CURLOPT_URL,url.c_str());
    curl_easy_setopt(handle,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT_MS,3000L);
    curl_easy_setopt(handle,CURLOPT_TIMEOUT_MS,7000L);
    curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(handle,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,cloudResponseWrite);
    curl_easy_setopt(handle,CURLOPT_WRITEDATA,&result);
    curl_easy_setopt(handle,CURLOPT_CUSTOMREQUEST,plan.method.c_str());
    if(!plan.body.empty()) {
        curl_easy_setopt(handle,CURLOPT_POSTFIELDS,plan.body.c_str());
        curl_easy_setopt(handle,CURLOPT_POSTFIELDSIZE,static_cast<long>(plan.body.size()));
    }
    const auto status=curl_easy_perform(handle);
    long responseCode=0;
    curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&responseCode);
    if(status!=CURLE_OK||responseCode<200||responseCode>=300)
        throw std::runtime_error("cloud API request failed");
    if(result.empty())return "affected=1";
    // Native templates only HTML-escape the returned text; do not attempt
    // to decode or interpolate it into SQL or HTML.
    return result;
}
#endif
