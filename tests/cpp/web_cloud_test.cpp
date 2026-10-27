// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
struct DbParameter {std::string name,value;};
struct DbConfiguration {std::string engine,endpointEnvironment,keyEnvironment,tokenEnvironment;};
bool validName(const std::string& name) {
    if(name.empty()||(!std::isalpha(static_cast<unsigned char>(name[0]))&&name[0]!='_'))return false;
    return std::all_of(name.begin()+1,name.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});
}
std::string dbJsonQuote(const std::string& data) {
    std::string out="\"";
    for(unsigned char c:data) {
        if(c=='"')out+="\\\"";
        else if(c=='\\')out+="\\\\";
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c<32)throw std::runtime_error("test control byte");
        else out+=char(c);
    }
    return out+"\"";
}
#include "web_native_cloud.hpp"
template<typename F>void rejected(F&& action) {
    bool failed=false;
    try {action();} catch(const std::runtime_error&) {failed=true;}
    assert(failed);
}
int main() {
    const auto select=prepareCloudRequest("supabase","GET messages?select=id,name&limit=20",{});
    assert(select.method=="GET"&&select.body.empty());
    auto insertion=prepareCloudRequest("supabase","POST messages",
        {{"name","Ada \" <script>"},{"city","Nairobi"}});
    assert(insertion.body=="{\"name\":\"Ada \\\" <script>\",\"city\":\"Nairobi\"}");
    assert(prepareCloudRequest("firebase","PATCH users/ada",{{"name","Ada"}}).body==
           "{\"name\":\"Ada\"}");
    assert(prepareCloudRequest("firebase","GET users/ada",{}).resource=="users/ada");
    assert(cloudOrigin("supabase","https://abc.supabase.co/")=="https://abc.supabase.co");
    assert(cloudOrigin("firebase","https://demo-default-rtdb.europe-west1.firebasedatabase.app")==
           "https://demo-default-rtdb.europe-west1.firebasedatabase.app");
    rejected([]{prepareCloudRequest("supabase","POST messages",{});});
    rejected([]{prepareCloudRequest("supabase","GET messages",{{"sql","DROP TABLE"}});});
    rejected([]{prepareCloudRequest("firebase","GET ../admin",{});});
    rejected([]{prepareCloudRequest("firebase","GET users/ada?auth=secret",{});});
    rejected([]{prepareCloudRequest("supabase","GET messages%2fadmin",{});});
    rejected([]{prepareCloudRequest("supabase","GET messages;DELETE",{});});
    rejected([]{prepareCloudRequest("supabase","POST messages",{{"a","1"},{"a","2"}});});
    rejected([]{cloudOrigin("supabase","https://abc.supabase.co.evil.org");});
    rejected([]{cloudOrigin("firebase","http://demo.firebaseio.com");});
    rejected([]{cloudOrigin("supabase","https://user:pass@abc.supabase.co");});
    rejected([]{cloudOrigin("firebase","https://foo.firebasedatabase.app:443");});
    std::cout<<"Cloud API plan and credential-origin safety tests passed\n";
}
