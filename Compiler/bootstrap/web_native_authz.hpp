// SPDX-License-Identifier: GPL-3.0-only
// Optional, fail-closed bearer authorization for native app routes.
// Secrets come from environment variables, never from the policy file.
#pragma once
#if !defined(_WIN32)
struct RoutePermission {
    std::string path,method,principal,token;
};
struct RouteAuthorization {
    bool allowed=true;
    std::string principal;
};

std::vector<RoutePermission> loadRoutePermissions() {
    const char* deny=std::getenv("NOQERI_WEB_AUTHZ_DEFAULT_DENY");
    if(deny&&std::string(deny)!="0"&&std::string(deny)!="1")
        throw std::runtime_error("NOQERI_WEB_AUTHZ_DEFAULT_DENY must be 0 or 1");
    const char* configured=std::getenv("NOQERI_WEB_AUTHZ_FILE");
    if(!configured||!*configured) {
        if(deny&&std::string(deny)=="1")
            throw std::runtime_error("default-deny route authorization requires a policy file");
        return {};
    }
    std::error_code ec;
    const fs::path source=configured;
    if(!fs::is_regular_file(source,ec)||fs::file_size(source,ec)>8192)
        throw std::runtime_error("native route authorization file missing or larger than 8 KiB");
    std::ifstream input(source);
    if(!input)throw std::runtime_error("cannot open native authorization file");
    std::vector<RoutePermission> entries;
    std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line[0]=='#')continue;
        if(entries.size()>=128)throw std::runtime_error("too many authorization policy rules");
        std::istringstream fields(line);
        RoutePermission rule;
        std::string tokenEnv,extra;
        if(!(fields>>rule.method>>rule.path>>tokenEnv)||(fields>>extra))
            throw std::runtime_error("authorization policy requires METHOD /path ENV_SECRET");
        if(rule.method!="GET"&&rule.method!="POST"&&rule.method!="BOTH")
            throw std::runtime_error("authorization method must be GET, POST or BOTH");
        if(!validRoute(rule.path)||dynamicRoute(rule.path)||
            rule.path.rfind("/__nq",0)==0)
            throw std::runtime_error("authorization path must be a literal route prefix");
        if(!validName(tokenEnv)||tokenEnv.size()>80)
            throw std::runtime_error("invalid authorization secret environment name");
        rule.principal=tokenEnv;
        const char* secret=std::getenv(tokenEnv.c_str());
        if(!secret)throw std::runtime_error("missing route authorization token environment variable: "+tokenEnv);
        rule.token=secret;
        if(rule.token.size()<32||rule.token.size()>512||
           rule.token.find_first_of("\r\n")!=std::string::npos)
            throw std::runtime_error("route bearer token must be 32..512 bytes");
        if(std::any_of(entries.begin(),entries.end(),[&](const RoutePermission& prior){
            return prior.path==rule.path&&prior.method==rule.method&&prior.principal==rule.principal;
        }))throw std::runtime_error("duplicate route authorization grant");
        entries.push_back(std::move(rule));
    }
    if(entries.empty())throw std::runtime_error("authorization policy is empty");
    for(const auto& rule:entries)if(rule.method=="POST") {
        const bool hasGet=std::any_of(entries.begin(),entries.end(),
            [&](const RoutePermission& candidate){
                return candidate.path==rule.path &&
                       candidate.principal==rule.principal &&
                       (candidate.method=="GET"||candidate.method=="BOTH");
            });
        if(!hasGet)
            throw std::runtime_error(
                "native CSRF-backed POST permission requires GET or BOTH grant for same principal and route");
    }
    return entries;
}

RouteAuthorization authorizeRoute(const std::vector<RoutePermission>& rules,
                                   const std::string& method,const std::string& path,
                                   const std::string& authorization) {
    std::size_t longest=0;bool protectedPath=false;
    for(const auto& rule:rules) {
        const bool matched=rule.path=="/"||path==rule.path||
            path.rfind(rule.path+"/",0)==0;
        if(matched && (!protectedPath||rule.path.size()>longest)){
            longest=rule.path.size();protectedPath=true;
        }
    }
    if(!protectedPath) {
        // Framework CSS/JS contains no session or application data and must
        // remain reachable by browsers that cannot add bearer headers to
        // module/style asset requests.
        if(path=="/theme.css"||path=="/__nq/reactive.js")return {};
        const char* deny=std::getenv("NOQERI_WEB_AUTHZ_DEFAULT_DENY");
        if(deny&&std::string(deny)=="1")return {false,{}};
        return {};
    }
    RouteAuthorization result;result.allowed=false;
    const std::string supplied=authorization.rfind("Bearer ",0)==0?
        authorization.substr(7):std::string{};
    for(const auto& rule:rules) {
        if(rule.path.size()!=longest)continue;
        if(!(rule.path=="/"||path==rule.path||path.rfind(rule.path+"/",0)==0))
            continue;
        if(rule.method!=method&&rule.method!="BOTH"&&
           !(method=="HEAD"&&rule.method=="GET"))continue;
        if(constantTimeEquals(supplied,rule.token)&&!supplied.empty()) {
            result.allowed=true;
            result.principal=rule.principal;
            return result;
        }
    }
    return result;
}
#endif
