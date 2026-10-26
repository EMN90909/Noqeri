// GPL-3.0-only test fixture for the NoeSQL ABI v2 wire adapter.
// NOT a real NoeSQL storage engine. Verifies callback invocation and binds.
#include "../../Compiler/bootstrap/web_native_noesql_v2.hpp"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <cstdint>

namespace {
int open(void*,const std::uint8_t* path,std::size_t size,void** out) {
    std::string file(reinterpret_cast<const char*>(path),size);
    if(file.size()<7||file.substr(file.size()-7)!=".noedb")return -1;
    *out=new int(1);return 0;
}
int close(void*,void* p) {delete static_cast<int*>(p);return 0;}
int prepare(void*,void*,const std::uint8_t* source,std::size_t size,void** out) {
    *out=new std::string(reinterpret_cast<const char*>(source),size);return 0;
}
int release(void*,void*,void* p){delete static_cast<std::string*>(p);return 0;}
int call(void*,void*,void* statement,const std::uint8_t* params,std::size_t n,
         std::uint8_t* out,std::size_t cap,std::size_t* written) {
    const auto& source=*static_cast<std::string*>(statement);
    if(const char* log=std::getenv("NOESQL_V2_MOCK_LOG")) {
        std::ofstream stream(log,std::ios::app);
        stream<<source<<"\n"<<std::string(reinterpret_cast<const char*>(params),n)<<"\n";
    }
    const std::string result=source.find(" get ")!=std::string::npos?
        R"([{"name":"native-v2-callback"}])":"affected=1";
    if(result.size()>cap)return -2;
    std::memcpy(out,result.data(),result.size());
    *written=result.size();return 0;
}
int tx(void*,void*){return 0;}
int err(void*,void*,std::uint8_t*,std::size_t,std::size_t* n){*n=0;return 0;}
const NqNoeSqlSessionV2 api{
    2,nullptr,open,close,prepare,release,call,call,tx,tx,tx,err
};
}
extern "C" const NqNoeSqlSessionV2* noesql_session_v2_get(){return &api;}
