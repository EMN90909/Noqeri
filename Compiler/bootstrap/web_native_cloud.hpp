// SPDX-License-Identifier: GPL-3.0-only
// Explicit server-side HTTPS adapters. Credentials never enter generated HTML.
#pragma once
#ifdef NOQERI_WEB_CURL
#include <curl/curl.h>

struct CloudStatement { std::string method, path; };
CloudStatement parseCloudStatement(const std::string& source,const std::string& engine,
                                   const std::vector<DbParameter>& params) {
    if(source.size()>2048)throw std::runtime_error("cloud statement too large");
    const auto space=source.find(' ');
    if(space==std::string::npos||source.find(' ',space+1)!=std::string::npos)
        throw std::runtime_error("cloud statement must be METHOD /path");
    CloudStatement command{source.substr(0,space),source.substr(space+1)};
    const bool read=command.method=="GET",remove=command.method=="DELETE";
    if(!read&&!remove&&command.method!="POST"&&command.method!="PATCH"&&command.method!="PUT")
        throw std::runtime_error("unsupported cloud request method");
    if((read||remove)&&!params.empty())
        throw std::runtime_error("GET/DELETE do not accept body bindings");
    if(!read&&!remove&&params.empty())
        throw std::runtime_error("cloud writes require named body parameters");
    if(command.path.empty()||command.path[0]!='/'||
       command.path.find("//")!=std::string::npos||
       command.path.find("..")!=std::string::npos||
       command.path.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_-.?=&%,:()*!$")!=std::string::npos)
        throw std::runtime_error("invalid cloud REST path");
    const auto suffix=command.path.substr(0,command.path.find('?'));
    if(engine=="firebase"&&(suffix.size()<5||suffix.substr(suffix.size()-5)!=".json"))
        throw std::runtime_error("Firebase Realtime Database paths must end in .json");
    if(engine=="supabase"&&command.method=="PUT")
        throw std::runtime_error("Supabase REST writes use POST/PATCH/DELETE");
    if(engine=="firestore") {
        // This native preview accepts one collection and an optional document ID.
        // It deliberately forbids user-provided Firestore query/mask fragments.
        if(command.method=="PUT"||command.path.find('?')!=std::string::npos||
           command.path.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_-")!=std::string::npos)
            throw std::runtime_error("Firestore requires /collection[/document] without query parameters");
        const auto segments=std::count(command.path.begin(),command.path.end(),'/');
        const bool document=segments==2&&command.path.back()!='/';
        if((segments!=1&&!document)||command.path=="/"||
           (command.method=="POST"&&document)||
           (command.method!="POST"&&command.method!="GET"&&!document))
            throw std::runtime_error("Firestore POST uses /collection; PATCH/DELETE use /collection/document");
    }
    return command;
}
std::string cloudEnvironment(const std::string& name,const char* kind) {
    const auto* setting=std::getenv(name.c_str());
    if(!setting||!*setting)throw std::runtime_error(std::string("missing cloud ")+kind+" environment variable");
    const std::string value(setting);
    if(value.size()>8192||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)
        throw std::runtime_error("invalid cloud credential/configuration");
    return value;
}
std::string executeCloud(const DbConfiguration& db,const std::string& source,
                         const std::vector<DbParameter>& params) {
    const CloudStatement statement=parseCloudStatement(source,db.engine,params);
    std::string endpoint=cloudEnvironment(db.dsnEnvironment,"endpoint");
    const auto key=cloudEnvironment(db.keyEnvironment,"token");
    const auto userToken=db.authEnvironment.empty()?std::string{}:cloudEnvironment(db.authEnvironment,"user token");
    if(endpoint.rfind("https://",0)!=0||endpoint.size()>1024||
       endpoint.find_first_of("?#@ \t\r\n\\")!=std::string::npos||
       endpoint.find("//",8)!=std::string::npos)
        throw std::runtime_error("cloud endpoint must be a trusted HTTPS base URL");
    if(endpoint.back()=='/')endpoint.pop_back();
    const auto validCredential=[](const std::string& value) {
        return value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._~-")==std::string::npos;
    };
    if(!validCredential(key)||!validCredential(userToken))
        throw std::runtime_error("cloud token contains unsupported characters");
    if(db.engine=="supabase"&&key.rfind("sb_secret_",0)==0) {
        // A secret key bypasses RLS. Require explicit server-side opt-in.
        const char* optIn=std::getenv("NOQERI_WEB_ALLOW_PRIVILEGED_KEY");
        if(!optIn||std::string(optIn)!="1")
            throw std::runtime_error("Supabase secret key needs privileged server opt-in");
        if(!userToken.empty())
            throw std::runtime_error("a Supabase secret key cannot be combined with user auth; use a publishable key for RLS");
    }
    std::string payload="{";
    std::set<std::string> seen;
    for(const auto& param:params) {
        if(!validName(param.name)||!seen.insert(param.name).second||param.value.size()>4096)
            throw std::runtime_error("invalid cloud body parameter");
        if(payload!="{")payload+=",";
        payload+=dbJsonQuote(param.name)+":"+dbJsonQuote(param.value);
    }
    payload+="}";
    if(db.engine=="firestore") {
        // Firestore's REST API uses tagged values, not PostgREST's plain JSON.
        // The native subset is intentionally text-only until typed NIR values land.
        std::string fields="{\"fields\":{";
        bool first=true;
        for(const auto& param:params) {
            if(!first)fields+=",";
            first=false;
            fields+=dbJsonQuote(param.name)+":{\"stringValue\":"+dbJsonQuote(param.value)+"}";
        }
        payload=fields+"}}";
    }
    if(payload.size()>16384)throw std::runtime_error("cloud request body too large");
    CURL* raw=curl_easy_init();
    if(!raw)throw std::runtime_error("cloud HTTPS client unavailable");
    struct Release {CURL* value;~Release(){curl_easy_cleanup(value);}} release{raw};
    struct curl_slist* headers=nullptr;
    struct FreeHeaders {curl_slist*& list;~FreeHeaders(){curl_slist_free_all(list);}} freeHeaders{headers};
    const auto addHeader=[&](const std::string& h) {
        auto* next=curl_slist_append(headers,h.c_str());
        if(!next)throw std::runtime_error("cloud header allocation failed");
        headers=next;
    };
    addHeader("Accept: application/json");
    if(db.engine=="supabase") {
        // sb_publishable_* is NOT a JWT. auth_env is a fixed server principal, not a per-visitor token. Only a JWT or
        // a legacy JWT-format API key belongs in the Bearer header.
        const bool legacyJwt=std::count(key.begin(),key.end(),'.')==2;
        if(!userToken.empty())addHeader("Authorization: Bearer "+userToken);
        else if(legacyJwt)addHeader("Authorization: Bearer "+key);
        addHeader("apikey: "+key);
        if(statement.method!="GET")addHeader("Prefer: return=representation");
    } else {
        addHeader("Authorization: Bearer "+key);
    }
    if(statement.method!="GET"&&statement.method!="DELETE")
        addHeader("Content-Type: application/json");
    std::string response;
    // Retain the URL buffer for the duration of the transfer.
    std::string url=endpoint+statement.path;
    if(db.engine=="firestore"&&(statement.method=="PATCH"||statement.method=="DELETE")) {
        url+="?currentDocument.exists=true";
        if(statement.method=="PATCH")
            for(const auto& param:params)url+="&updateMask.fieldPaths="+param.name;
    }
    curl_easy_setopt(raw,CURLOPT_URL,url.c_str());
    curl_easy_setopt(raw,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
    curl_easy_setopt(raw,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(raw,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(raw,CURLOPT_SSL_VERIFYHOST,2L);
    // libcurl does not portably honor the curl *command-line* CA env vars.
    // A trusted server operator may configure an explicit private CA file.
    // This never disables TLS peer/hostname verification.
    if(const char* ca=std::getenv("NOQERI_WEB_CA_BUNDLE")) {
        const std::string caFile(ca);
        if(caFile.empty()||caFile.size()>2048||
           caFile.find_first_of("\r\n\0",0,3)!=std::string::npos)
            throw std::runtime_error("invalid native HTTPS CA bundle configuration");
        curl_easy_setopt(raw,CURLOPT_CAINFO,caFile.c_str());
    }
    curl_easy_setopt(raw,CURLOPT_CONNECTTIMEOUT,3L);
    curl_easy_setopt(raw,CURLOPT_TIMEOUT,10L);
    curl_easy_setopt(raw,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(raw,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(raw,CURLOPT_CUSTOMREQUEST,statement.method.c_str());
    if(statement.method!="GET"&&statement.method!="DELETE") {
        curl_easy_setopt(raw,CURLOPT_POSTFIELDS,payload.data());
        curl_easy_setopt(raw,CURLOPT_POSTFIELDSIZE,static_cast<long>(payload.size()));
    }
    curl_easy_setopt(raw,CURLOPT_WRITEFUNCTION,+[](char* bytes,size_t size,size_t count,void* context)->size_t {
        if(count && size>static_cast<size_t>(-1)/count)return 0;
        const auto len=size*count;
        auto& output=*static_cast<std::string*>(context);
        if(len>65536-output.size())return 0;
        output.append(bytes,len);
        return len;
    });
    curl_easy_setopt(raw,CURLOPT_WRITEDATA,&response);
    const auto result=curl_easy_perform(raw);
    long status=0;
    if(result!=CURLE_OK||curl_easy_getinfo(raw,CURLINFO_RESPONSE_CODE,&status)!=CURLE_OK||
       status<200||status>=300)
        throw std::runtime_error("cloud provider request failed");
    if(response.empty())return "[]";
    return response;
}
#endif
