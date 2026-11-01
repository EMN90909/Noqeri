#include "noe.hpp"
#include "abi.h"
#include "noqeri/network_transport.hpp"
#include "noqeri/network_dns.hpp"
#include "noqeri/network_wireless.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace noe {
namespace {
std::string valueText(const noqeri_abi_value& value){switch(value.tag){case NOQERI_ABI_NULL:return"null";case NOQERI_ABI_BOOL:return value.as.boolean?"true":"false";case NOQERI_ABI_INT:return std::to_string(value.as.integer);case NOQERI_ABI_FLOAT:return std::to_string(value.as.floating);case NOQERI_ABI_STRING:return value.as.string.data?std::string(value.as.string.data,value.as.string.size):std::string{};default:return"<invalid>";}}
int32_t desktopWrite(void*,const char* data,size_t size){if(data&&size)std::cout.write(data,static_cast<std::streamsize>(size));return std::cout?0:1;}
int64_t desktopClock(void*){return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
noqeri_abi_string desktopPlatform(void*){
#if defined(_WIN32)
static constexpr char text[]="windows";
#elif defined(__APPLE__)
static constexpr char text[]="macos";
#elif defined(__linux__)
static constexpr char text[]="linux";
#else
static constexpr char text[]="standalone";
#endif
return{text,sizeof(text)-1};}
const noqeri_abi desktopAbi={NOQERI_ABI_VERSION,sizeof(noqeri_abi),nullptr,desktopWrite,desktopClock,desktopPlatform,0,nullptr};
noqeri_abi_value toAbi(const NirValue& value){noqeri_abi_value out{};if(std::holds_alternative<std::monostate>(value))out.tag=NOQERI_ABI_NULL;else if(auto v=std::get_if<bool>(&value)){out.tag=NOQERI_ABI_BOOL;out.as.boolean=*v?1:0;}else if(auto v=std::get_if<std::int64_t>(&value)){out.tag=NOQERI_ABI_INT;out.as.integer=*v;}else if(auto v=std::get_if<double>(&value)){out.tag=NOQERI_ABI_FLOAT;out.as.floating=*v;}else if(auto v=std::get_if<std::string>(&value)){out.tag=NOQERI_ABI_STRING;out.as.string={v->data(),v->size()};}return out;}
NirValue fromAbi(const noqeri_abi_value& value){switch(value.tag){case NOQERI_ABI_NULL:return std::monostate{};case NOQERI_ABI_BOOL:return value.as.boolean!=0;case NOQERI_ABI_INT:return static_cast<std::int64_t>(value.as.integer);case NOQERI_ABI_FLOAT:return value.as.floating;case NOQERI_ABI_STRING:return value.as.string.data?std::string(value.as.string.data,value.as.string.size):std::string{};default:throw std::runtime_error("ABI returned invalid value tag");}}
}
const noqeri_abi* defaultNoqeriAbi(){return &desktopAbi;}
std::optional<NirValue> callNoqeriAbi(const noqeri_abi* abi,const std::string& name,const std::vector<NirValue>& args,std::string& error){
    if(!abi)return std::nullopt;
    if(abi->abi_version!=NOQERI_ABI_VERSION||abi->struct_size<sizeof(noqeri_abi)){error="Noqeri ABI version/size mismatch";return std::nullopt;}
    if(name=="print"){
        if(!abi->write){error="Noqeri ABI has no write callback";return std::nullopt;}
        for(std::size_t i=0;i<args.size();++i){if(i&&abi->write(abi->context," ",1)!=0){error="ABI write failed";return std::nullopt;}auto v=toAbi(args[i]);auto text=valueText(v);if(abi->write(abi->context,text.data(),text.size())!=0){error="ABI write failed";return std::nullopt;}}
        if(abi->write(abi->context,"\n",1)!=0){error="ABI write failed";return std::nullopt;}return NirValue(std::monostate{});
    }
    if(name=="clockMillis"){if(!args.empty()){error="clockMillis expects no arguments";return std::nullopt;}if(!abi->clock_millis){error="Noqeri ABI has no clock_millis callback";return std::nullopt;}return NirValue(static_cast<std::int64_t>(abi->clock_millis(abi->context)));}
    if(name=="platform"){if(!args.empty()){error="platform expects no arguments";return std::nullopt;}if(!abi->platform_name){error="Noqeri ABI has no platform_name callback";return std::nullopt;}auto s=abi->platform_name(abi->context);return NirValue(s.data?std::string(s.data,s.size):std::string{});}
    if(name=="textLength"){if(args.size()!=1||!std::holds_alternative<std::string>(args[0])){error="textLength expects one string argument";return std::nullopt;}return NirValue(static_cast<std::int64_t>(std::get<std::string>(args[0]).size()));}
    for(size_t i=0;i<abi->service_count;++i){const auto& service=abi->services[i];if(service.name&&service.invoke&&name==service.name){std::vector<noqeri_abi_value> converted;converted.reserve(args.size());for(const auto& value:args)converted.push_back(toAbi(value));noqeri_abi_value result{};noqeri_abi_error abiError{};int32_t status=service.invoke(service.context?service.context:abi->context,converted.data(),converted.size(),&result,&abiError);if(status!=0){error=abiError.message?abiError.message:"Noqeri ABI service failed";return std::nullopt;}return fromAbi(result);}}
    // Built-in networking is available to the desktop interpreter only.
    // Custom embedding hosts may supply their own service implementations and
    // are never implicitly granted network access through this fallback.
    if (abi != &desktopAbi) return std::nullopt;
    static thread_local int network_last_error = 0;
    auto net_str = [&](std::size_t n) -> const std::string* {
        return n < args.size() ? std::get_if<std::string>(&args[n]) : nullptr;
    };
    auto net_int = [&](std::size_t n, std::int64_t& out) -> bool {
        if (n >= args.size()) return false;
        if (auto v = std::get_if<std::int64_t>(&args[n])) { out = *v; return true; }
        return false;
    };
    auto bad = [&]() -> std::optional<NirValue> {
        error = "network function received invalid argument types or count";
        network_last_error = -2;
        return std::nullopt;
    };
    if (name == "netLastError") {
        if (!args.empty()) return bad();
        return NirValue(static_cast<std::int64_t>(network_last_error));
    }
    if (name == "dnsResolveFirst" || name == "netDnsResolve") {
        std::int64_t family = 0;
        auto host = net_str(0);
        if (args.size()!=2 || !host || !net_int(1, family) ||
            (family!=0 && family!=4 && family!=6)) return bad();
        auto result = noqeri::network::resolve_host(*host, family==4);
        if (!result.ok()) { network_last_error = -1; return NirValue(std::string{}); }
        for (const auto& address : result.addresses)
            if (family != 6 || address.find(':') != std::string::npos) {
                network_last_error = 0;
                return NirValue(address);
            }
        network_last_error = -1;
        return NirValue(std::string{});
    }
    if (name == "netHttpAvailable") {
        if (!args.empty()) return bad();
        return NirValue(noqeri::network::http_available());
    }
    static thread_local std::string wifi_scan_cache, bluetooth_scan_cache;
    auto line_count = [](const std::string& lines) -> std::int64_t {
        std::int64_t count = 0;
        std::size_t start = 0;
        while (start < lines.size()) {
            auto end = lines.find('\n', start);
            if (end == std::string::npos) end = lines.size();
            if (end > start) ++count;
            start = end + 1;
        }
        return count;
    };
    if (name=="wifiAvailable" || name=="netWifiAvailable") {
        if (!args.empty()) return bad();
        return NirValue(noqeri::network::wifi_available());
    }
    if (name=="wifiConnectedSsid" || name=="netWifiSsid") {
        if (!args.empty()) return bad();
        auto r=noqeri::network::wifi_connected_ssid();
        network_last_error=r.error;
        return NirValue(r.value);
    }
    if (name=="wifiScan" || name=="netWifiScan") {
        if (!args.empty()) return bad();
        auto r=noqeri::network::wifi_scan();
        network_last_error=r.error;
        wifi_scan_cache=r.value;
        if (name=="wifiScan") return NirValue(r.error ? static_cast<std::int64_t>(r.error) : line_count(r.value));
        return NirValue(r.value);
    }
    if (name=="wifiScanResults") {
        if (!args.empty()) return bad();
        return NirValue(wifi_scan_cache);
    }
    if (name=="wifiConnect" || name=="netWifiConnectSaved") {
        auto ssid=net_str(0);
        if (!ssid || (name=="wifiConnect" ? args.size()!=2 || !net_str(1) : args.size()!=1)) return bad();
        if (name=="wifiConnect" && !net_str(1)->empty()) {
            network_last_error=-3; // credentials must not be exposed in command line
            return NirValue(static_cast<std::int64_t>(-3));
        }
        auto rc=noqeri::network::wifi_connect_saved(*ssid);
        network_last_error=rc;
        return NirValue(static_cast<std::int64_t>(rc));
    }
    if (name=="wifiDisconnect" || name=="netWifiDisconnect") {
        if (!args.empty()) return bad();
        auto rc=noqeri::network::wifi_disconnect();
        network_last_error=rc;
        return NirValue(static_cast<std::int64_t>(rc));
    }
    if (name=="bluetoothAvailable" || name=="netBluetoothAvailable") {
        if (!args.empty()) return bad();
        return NirValue(noqeri::network::bluetooth_available());
    }
    if (name=="bluetoothScan" || name=="netBluetoothScan") {
        std::int64_t ms=0;
        if (args.size()!=1 || !net_int(0,ms)) return bad();
        if (ms<1000 || ms>30000) return bad();
        auto r=noqeri::network::bluetooth_scan(static_cast<int>(ms));
        network_last_error=r.error;
        bluetooth_scan_cache=r.value;
        if (name=="bluetoothScan")
            return NirValue(r.error ? static_cast<std::int64_t>(r.error) : line_count(r.value));
        return NirValue(r.value);
    }
    if (name=="bluetoothScanResults") {
        if (!args.empty()) return bad();
        return NirValue(bluetooth_scan_cache);
    }
    if (name=="bluetoothConnect" || name=="netBluetoothConnect") {
        auto id=net_str(0); std::int64_t ms=0;
        if (args.size()!=2 || !id || !net_int(1,ms)) return bad();
        if (ms<1000 || ms>30000) return bad();
        auto handle=noqeri::network::bluetooth_connect(*id,static_cast<int>(ms));
        network_last_error=handle<0?static_cast<int>(handle):0;
        return NirValue(handle);
    }
    if (name=="bluetoothDisconnect" || name=="netBluetoothDisconnect") {
        std::int64_t handle=0;
        if (args.size()!=1 || !net_int(0,handle)) return bad();
        auto rc=noqeri::network::bluetooth_disconnect(handle);
        network_last_error=rc;
        return NirValue(static_cast<std::int64_t>(rc));
    }
    if (name == "netTcpConnect" || name == "tcpConnect") {
        std::int64_t port=0, timeout=0;
        auto host=net_str(0);
        if (args.size()!=3 || !host || !net_int(1,port) || !net_int(2,timeout)) return bad();
        auto id=noqeri::network::tcp_connect(*host,static_cast<int>(port),static_cast<int>(timeout));
        network_last_error = id < 0 ? static_cast<int>(id) : 0;
        return NirValue(id);
    }
    if (name == "netTcpListen" || name == "tcpListen") {
        std::int64_t port=0,backlog=0;
        auto host=net_str(0);
        if (args.size()!=3 || !host || !net_int(1,port) || !net_int(2,backlog)) return bad();
        auto id=noqeri::network::tcp_listen(*host,static_cast<int>(port),static_cast<int>(backlog));
        network_last_error = id < 0 ? static_cast<int>(id) : 0;
        return NirValue(id);
    }
    if (name == "netTcpAccept" || name == "tcpAccept") {
        std::int64_t id=0,timeout=30000;
        if ((args.size()!=2 && !(name=="tcpAccept"&&args.size()==1)) ||
            !net_int(0,id) || (args.size()==2 && !net_int(1,timeout))) return bad();
        auto new_id=noqeri::network::tcp_accept(id,static_cast<int>(timeout));
        network_last_error = new_id < 0 ? static_cast<int>(new_id) : 0;
        return NirValue(new_id);
    }
    if (name == "netTcpSend") {
        std::int64_t id=0,timeout=0; auto bytes=net_str(1);
        if (args.size()!=3 || !net_int(0,id) || !bytes || !net_int(2,timeout)) return bad();
        auto n=noqeri::network::tcp_send(id,*bytes,static_cast<int>(timeout));
        network_last_error = n < 0 ? n : 0;
        return NirValue(static_cast<std::int64_t>(n));
    }
    if (name == "netTcpRead" || name == "netUdpRead") {
        std::int64_t id=0,max_bytes=0,timeout=0;
        if (args.size()!=3 || !net_int(0,id) || !net_int(1,max_bytes) || !net_int(2,timeout)) return bad();
        auto result = name=="netTcpRead" ?
            noqeri::network::tcp_receive(id,static_cast<int>(max_bytes),static_cast<int>(timeout)) :
            noqeri::network::udp_receive(id,static_cast<int>(max_bytes),static_cast<int>(timeout));
        network_last_error=result.error;
        return NirValue(result.value);
    }
    if (name == "netUdpBind" || name == "udpBind") {
        std::int64_t port=0; auto host=net_str(0);
        if (args.size()!=2 || !host || !net_int(1,port)) return bad();
        auto id=noqeri::network::udp_bind(*host,static_cast<int>(port));
        network_last_error = id < 0 ? static_cast<int>(id) : 0;
        return NirValue(id);
    }
    if (name == "netUdpSend") {
        std::int64_t id=0,port=0,timeout=0;
        auto host=net_str(1); auto bytes=net_str(3);
        if (args.size()!=5 || !net_int(0,id) || !host || !net_int(2,port) ||
            !bytes || !net_int(4,timeout)) return bad();
        auto n=noqeri::network::udp_send_to(id,*host,static_cast<int>(port),*bytes,static_cast<int>(timeout));
        network_last_error = n < 0 ? n : 0;
        return NirValue(static_cast<std::int64_t>(n));
    }
    if (name == "netSocketLocalPort" || name == "netSocketClose" ||
        name == "tcpClose" || name == "udpClose") {
        std::int64_t id=0;
        if (args.size()!=1 || !net_int(0,id)) return bad();
        auto n=name=="netSocketLocalPort" ? noqeri::network::socket_local_port(id) :
                    noqeri::network::socket_close(id);
        network_last_error = n < 0 ? n : 0;
        if (name == "tcpClose" || name == "udpClose") return NirValue(std::monostate{});
        return NirValue(static_cast<std::int64_t>(n));
    }
    if (name == "netHttpRequest" || name == "httpClientRequest") {
        auto method=net_str(0); auto url=net_str(1); auto body=net_str(2);
        std::int64_t timeout=30000;
        if ((args.size()!=4 && !(name=="httpClientRequest"&&args.size()==3)) ||
            !method || !url || !body || (args.size()==4 && !net_int(3,timeout))) return bad();
        auto id=noqeri::network::http_request(*method,*url,*body,static_cast<int>(timeout));
        network_last_error = id < 0 ? static_cast<int>(id) : 0;
        return NirValue(id);
    }
    if (name == "netHttpStatus" || name == "httpClientStatus" ||
        name == "netHttpBody" || name == "httpClientBody" ||
        name == "netHttpClose" || name == "httpClientClose") {
        std::int64_t id=0;
        if (args.size()!=1 || !net_int(0,id)) return bad();
        if (name=="netHttpBody"||name=="httpClientBody") {
            auto r=noqeri::network::http_body(id);
            network_last_error=r.error;
            return NirValue(r.value);
        }
        if (name=="netHttpClose"||name=="httpClientClose") {
            int rc=noqeri::network::http_close(id);
            network_last_error=rc;
            return name=="httpClientClose" ? NirValue(std::monostate{}) :
                   NirValue(static_cast<std::int64_t>(rc));
        }
        int status=noqeri::network::http_status(id);
        network_last_error=status < 0 ? status : 0;
        return NirValue(static_cast<std::int64_t>(status));
    }
    if (name == "netHttpHeader" || name == "httpClientHeader") {
        std::int64_t id=0;auto header=net_str(1);
        if (args.size()!=2 || !net_int(0,id) || !header) return bad();
        auto r=noqeri::network::http_header(id,*header);
        network_last_error=r.error;
        return NirValue(r.value);
    }
    return std::nullopt;
}
const noqeri_abi* defaultHostApi(){return defaultNoqeriAbi();}
std::optional<NirValue> callAbiFunction(const noqeri_abi* abi,const std::string& name,const std::vector<NirValue>& args,std::string& error){return callNoqeriAbi(abi,name,args,error);}
}
extern "C" uint32_t noqeri_abi_version(void){return NOQERI_ABI_VERSION;}
extern "C" int32_t noqeri_run_source(const char* source,const noqeri_abi* abi,noqeri_abi_error* error){if(!source){if(error){error->code=1;error->message="source must not be null";}return 1;}auto result=noe::compileSource(source);if(result.diagnostics.hasErrors()){if(error){error->code=2;error->message="source failed to compile";}return 2;}int status=noe::Interpreter(abi).run(result.nir);if(status!=0&&error){error->code=3;error->message="runtime execution failed";}return status==0?0:3;}
