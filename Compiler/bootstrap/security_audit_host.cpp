#include "noe.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace noe {
namespace {
std::string readOptional(const std::filesystem::path&path){std::ifstream in(path,std::ios::binary);if(!in)return{};std::ostringstream out;out<<in.rdbuf();return out.str();}
struct Capability { const char* needle; const char* label; };

bool validateLockedDependency(const std::string&lock,bool v3,const std::string&alias,const DependencySpec&spec){
    const auto prefix=(v3?"package ":"dependency ")+alias+" ";
    const auto at=lock.find(prefix);
    if(at==std::string::npos){std::cerr<<"fail dependency not locked: "<<alias<<"\n";return false;}
    const auto end=lock.find('\n',at);
    const auto line=lock.substr(at,end==std::string::npos?std::string::npos:end-at);
    if(!v3){
        if(line.find("checksum sha256:")==std::string::npos){std::cerr<<"fail dependency lacks SHA-256 lock identity: "<<alias<<"\n";return false;}
        return true;
    }
    std::istringstream fields(line);
    std::string keyword,lockedAlias,coordinate,version,integrity,entry;
    if(!(fields>>keyword>>lockedAlias>>coordinate>>version>>integrity>>entry)||keyword!="package"||lockedAlias!=alias){
        std::cerr<<"fail malformed noqeri-lock 3 package entry: "<<alias<<"\n";return false;
    }
    if(coordinate!=spec.package){std::cerr<<"fail locked package coordinate mismatch for "<<alias<<": "<<coordinate<<" != "<<spec.package<<"\n";return false;}
    if(!spec.version.empty()&&version!=spec.version){std::cerr<<"fail locked package version mismatch for "<<alias<<": "<<version<<" != "<<spec.version<<"\n";return false;}
    if(integrity.rfind("sha256:",0)!=0||integrity.size()!=71){std::cerr<<"fail dependency lacks SHA-256 lock identity: "<<alias<<"\n";return false;}
    return true;
}
} // namespace

int runSecurityAudit(const std::filesystem::path&root){
    int integrityFailures=0;std::size_t reviews=0;Diagnostics diagnostics;auto manifestPath=root/"project.noqeri";auto manifest=PackageManager{}.loadManifest(manifestPath,diagnostics);
    if(!manifest){++integrityFailures;diagnostics.print(manifestPath.string());}else{
        auto lockPath=root/"noqeri.lock";auto lock=readOptional(lockPath);
        const bool lock2=lock.rfind("noqeri-lock 2\n",0)==0;
        const bool lock3=lock.rfind("noqeri-lock 3\n",0)==0;
        if(!lock2&&!lock3){std::cerr<<"fail package lock missing or unsupported (expected content-addressed noqeri-lock 2 or 3): "<<lockPath.string()<<"\n";++integrityFailures;}
        else for(const auto&[alias,spec]:manifest->dependencySpecs)if(!validateLockedDependency(lock,lock3,alias,spec))++integrityFailures;
    }
    const std::vector<Capability>capabilities={{"asm(\"","inline assembly"},{"intrinsic(\"","target intrinsic"},{"*volatile ","volatile memory"},{"host(\"","host service"},{"abi(\"","ABI service"},{" as *","pointer cast"}};
    std::error_code ec;if(std::filesystem::exists(root,ec))for(const auto&entry:std::filesystem::recursive_directory_iterator(root,ec)){if(ec)break;if(!entry.is_regular_file())continue;auto path=entry.path();if(path.extension()!=".noqeri")continue;auto generic=path.generic_string();if(generic.find("/.git/")!=std::string::npos||generic.find("/build/")!=std::string::npos||generic.find("/.noqeri/vendor-home/")!=std::string::npos)continue;auto source=readOptional(path);for(const auto&cap:capabilities){std::size_t pos=0,count=0;while((pos=source.find(cap.needle,pos))!=std::string::npos){++count;++reviews;pos+=std::char_traits<char>::length(cap.needle);}if(count)std::cout<<"review "<<cap.label<<" count="<<count<<" file="<<path.string()<<"\n";}}
    if(ec){std::cerr<<"fail source audit traversal: "<<ec.message()<<"\n";++integrityFailures;}
    std::cout<<"audit integrity="<<(integrityFailures?"failed":"ok")<<" capability-review="<<reviews<<"\n";
    std::cout<<"note native audit checks source capabilities and lock integrity; stage-1 `noqeri audit` additionally checks the registry advisory feed\n";
    if(reviews)std::cout<<"note capability findings are review points, not automatic vulnerabilities\n";
    return integrityFailures?1:0;
}

} // namespace noe
