#include "noe.hpp"
#include "abi.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/file.h>
#else
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <sys/stat.h>
#include <cstring>
#include <array>
#include <iomanip>
#include <sstream>

namespace noe {
namespace {

class HostSha256 {
public:
    HostSha256(){reset();}
    void update(const unsigned char*data,std::size_t len){for(std::size_t i=0;i<len;++i){buffer_[bufferLength_++]=data[i];if(bufferLength_==64){transform();bitLength_+=512;bufferLength_=0;}}}
    void finishRaw(unsigned char* out){
        std::size_t i=bufferLength_;buffer_[i++]=0x80;
        if(i>56){while(i<64)buffer_[i++]=0;transform();i=0;}
        while(i<56)buffer_[i++]=0;
        bitLength_+=static_cast<std::uint64_t>(bufferLength_)*8;
        for(int j=7;j>=0;--j)buffer_[i++]=static_cast<unsigned char>((bitLength_>>(j*8))&0xff);
        transform();
        for(std::size_t w=0; w<8; ++w){
            out[w*4+0] = static_cast<unsigned char>((state_[w] >> 24) & 0xff);
            out[w*4+1] = static_cast<unsigned char>((state_[w] >> 16) & 0xff);
            out[w*4+2] = static_cast<unsigned char>((state_[w] >> 8) & 0xff);
            out[w*4+3] = static_cast<unsigned char>(state_[w] & 0xff);
        }
        reset();
    }
private:
    static std::uint32_t rotr(std::uint32_t x,std::uint32_t n){return(x>>n)|(x<<(32-n));}
    void reset(){state_={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};bufferLength_=0;bitLength_=0;}
    void transform(){
        static constexpr std::array<std::uint32_t,64>k={
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };
        std::uint32_t w[64]{};
        for(std::size_t i=0;i<16;++i)w[i]=(static_cast<std::uint32_t>(buffer_[i*4])<<24)|(static_cast<std::uint32_t>(buffer_[i*4+1])<<16)|(static_cast<std::uint32_t>(buffer_[i*4+2])<<8)|buffer_[i*4+3];
        for(std::size_t i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(std::size_t i=0;i<64;++i){auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=h+s1+ch+k[i]+w[i];auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    std::array<std::uint32_t,8>state_{};std::array<unsigned char,64>buffer_{};std::size_t bufferLength_=0;std::uint64_t bitLength_=0;
};
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

    auto getI64 = [](const NirValue& v) -> std::int64_t {
        if (auto p = std::get_if<std::int64_t>(&v)) return *p;
        if (auto p = std::get_if<bool>(&v)) return *p ? 1 : 0;
        return 0;
    };
    auto getStr = [](const NirValue& v) -> std::string {
        if (auto s = std::get_if<std::string>(&v)) return *s;
        return {};
    };

    if (name == "fileOpen") {
        if (args.size() < 2) { error = "fileOpen requires path and flags"; return std::nullopt; }
        std::string path = getStr(args[0]);
        uint32_t flags = static_cast<uint32_t>(getI64(args[1]));
        int openFlags = 0;
        bool canRead = (flags & 1) != 0;
        bool canWrite = (flags & 2) != 0;
        if (canRead && canWrite) openFlags |= O_RDWR;
        else if (canWrite) openFlags |= O_WRONLY;
        else openFlags |= O_RDONLY;
        if (flags & 4) openFlags |= O_CREAT;
        if (flags & 8) openFlags |= O_TRUNC;
        if (flags & 16) openFlags |= O_APPEND;
        int fd = open(path.c_str(), openFlags, 0600);
        return NirValue(static_cast<std::int64_t>(fd < 0 ? -errno : fd));
    }
    if (name == "fileClose") {
        if (args.empty()) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        return NirValue(static_cast<std::int64_t>(close(fd) < 0 ? -errno : 0));
    }
    if (name == "fileRead") {
        if (args.size() < 3) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        uintptr_t ptr = static_cast<uintptr_t>(getI64(args[1]));
        size_t count = static_cast<size_t>(getI64(args[2]));
        if (!ptr && count > 0) return NirValue(static_cast<std::int64_t>(-1));
        ssize_t n = read(fd, reinterpret_cast<void*>(ptr), count);
        return NirValue(static_cast<std::int64_t>(n < 0 ? -errno : n));
    }
    if (name == "fileWrite") {
        if (args.size() < 3) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        uintptr_t ptr = static_cast<uintptr_t>(getI64(args[1]));
        size_t count = static_cast<size_t>(getI64(args[2]));
        if (!ptr && count > 0) return NirValue(static_cast<std::int64_t>(-1));
        ssize_t n = write(fd, reinterpret_cast<const void*>(ptr), count);
        return NirValue(static_cast<std::int64_t>(n < 0 ? -errno : n));
    }
    if (name == "fileSeek") {
        if (args.size() < 3) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        off_t offset = static_cast<off_t>(getI64(args[1]));
        int origin = static_cast<int>(getI64(args[2]));
        int whence = (origin == 0) ? SEEK_SET : ((origin == 1) ? SEEK_CUR : SEEK_END);
        off_t res = lseek(fd, offset, whence);
        return NirValue(static_cast<std::int64_t>(res < 0 ? -errno : res));
    }
    if (name == "fileFlush") {
        return NirValue(static_cast<std::int64_t>(0));
    }
    if (name == "fileSync") {
        if (args.empty()) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
#if !defined(_WIN32)
        return NirValue(static_cast<std::int64_t>(fsync(fd) < 0 ? -errno : 0));
#else
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h == INVALID_HANDLE_VALUE) return NirValue(static_cast<std::int64_t>(-1));
        return NirValue(static_cast<std::int64_t>(FlushFileBuffers(h) ? 0 : -static_cast<std::int64_t>(GetLastError())));
#endif
    }
    if (name == "fileTruncate") {
        if (args.size() < 2) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        off_t length = static_cast<off_t>(getI64(args[1]));
#if !defined(_WIN32)
        return NirValue(static_cast<std::int64_t>(ftruncate(fd, length) < 0 ? -errno : 0));
#else
        return NirValue(static_cast<std::int64_t>(_chsize_s(fd, length) != 0 ? -errno : 0));
#endif
    }
    if (name == "fileLock") {
        if (args.size() < 2) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
        bool exclusive = (getI64(args[1]) != 0);
#if !defined(_WIN32)
        int op = (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
        return NirValue(static_cast<std::int64_t>(flock(fd, op) < 0 ? -errno : 0));
#else
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h == INVALID_HANDLE_VALUE) return NirValue(static_cast<std::int64_t>(-1));
        DWORD flags = (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) | LOCKFILE_FAIL_IMMEDIATELY;
        OVERLAPPED ov{};
        BOOL res = LockFileEx(h, flags, 0, 0xFFFFFFFF, 0xFFFFFFFF, &ov);
        return NirValue(static_cast<std::int64_t>(res ? 0 : -static_cast<std::int64_t>(GetLastError())));
#endif
    }
    if (name == "fileUnlock") {
        if (args.empty()) return std::nullopt;
        int fd = static_cast<int>(getI64(args[0]));
#if !defined(_WIN32)
        return NirValue(static_cast<std::int64_t>(flock(fd, LOCK_UN) < 0 ? -errno : 0));
#else
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h == INVALID_HANDLE_VALUE) return NirValue(static_cast<std::int64_t>(-1));
        OVERLAPPED ov{};
        BOOL res = UnlockFileEx(h, 0, 0xFFFFFFFF, 0xFFFFFFFF, &ov);
        return NirValue(static_cast<std::int64_t>(res ? 0 : -static_cast<std::int64_t>(GetLastError())));
#endif
    }
    if (name == "fsDirectorySync") {
        if (args.empty()) return std::nullopt;
        std::string path = getStr(args[0]);
#if !defined(_WIN32)
        int dfd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) {
            int r = fsync(dfd);
            int err = errno;
            close(dfd);
            return NirValue(static_cast<std::int64_t>(r < 0 ? -err : 0));
        }
        return NirValue(static_cast<std::int64_t>(-errno));
#else
        HANDLE hDir = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hDir != INVALID_HANDLE_VALUE) {
            BOOL res = FlushFileBuffers(hDir);
            DWORD err = GetLastError();
            CloseHandle(hDir);
            return NirValue(static_cast<std::int64_t>(res ? 0 : -static_cast<std::int64_t>(err)));
        }
        return NirValue(static_cast<std::int64_t>(-static_cast<std::int64_t>(GetLastError())));
#endif
    }
    if (name == "fsExists") {
        if (args.empty()) return std::nullopt;
        return NirValue(std::filesystem::exists(getStr(args[0])));
    }
    if (name == "fsIsDirectory") {
        if (args.empty()) return std::nullopt;
        return NirValue(std::filesystem::is_directory(getStr(args[0])));
    }
    if (name == "fsIsFile") {
        if (args.empty()) return std::nullopt;
        return NirValue(std::filesystem::is_regular_file(getStr(args[0])));
    }
    if (name == "fsCreateDirectory") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::create_directory(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "fsCreateDirectories") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::create_directories(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "fsRemove") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::remove(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "fsRemoveTree") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::remove_all(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "fsRename") {
        if (args.size() < 2) return std::nullopt;
        std::string from = getStr(args[0]);
        std::string to = getStr(args[1]);
#if !defined(_WIN32)
        int r = rename(from.c_str(), to.c_str());
        return NirValue(static_cast<std::int64_t>(r < 0 ? -errno : 0));
#else
        BOOL ok = MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        return NirValue(static_cast<std::int64_t>(ok ? 0 : -static_cast<std::int64_t>(GetLastError())));
#endif
    }
    if (name == "fsCopyFile") {
        if (args.size() < 2) return std::nullopt;
        bool overwrite = args.size() >= 3 && (getI64(args[2]) != 0);
        std::error_code ec;
        std::filesystem::copy_file(getStr(args[0]), getStr(args[1]),
            overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none, ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "fsFileSize") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        auto sz = std::filesystem::file_size(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : static_cast<std::int64_t>(sz)));
    }
    if (name == "fsModifiedMillis") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(getStr(args[0]), ec);
        if (ec) return NirValue(static_cast<std::int64_t>(-ec.value()));
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            mtime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
        return NirValue(static_cast<std::int64_t>(ms));
    }
    if (name == "fsCanonical") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        auto p = std::filesystem::canonical(getStr(args[0]), ec);
        return NirValue(ec ? getStr(args[0]) : p.string());
    }
    if (name == "fsCurrentDirectory") {
        return NirValue(std::filesystem::current_path().string());
    }
    if (name == "fsSetCurrentDirectory") {
        if (args.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::current_path(getStr(args[0]), ec);
        return NirValue(static_cast<std::int64_t>(ec ? -ec.value() : 0));
    }
    if (name == "noeHostSha256") {
        if (args.size() < 4) return std::nullopt;
        uintptr_t src = static_cast<uintptr_t>(getI64(args[0]));
        size_t len = static_cast<size_t>(getI64(args[1]));
        uintptr_t dst = static_cast<uintptr_t>(getI64(args[2]));
        size_t cap = static_cast<size_t>(getI64(args[3]));
        if (cap < 32 || (!src && len > 0) || !dst) return NirValue(static_cast<std::int64_t>(-1));
        HostSha256 sha;
        if (len > 0) sha.update(reinterpret_cast<const unsigned char*>(src), len);
        sha.finishRaw(reinterpret_cast<unsigned char*>(dst));
        return NirValue(static_cast<std::int64_t>(0));
    }
    if (name == "stringToBytes") {
        if (args.size() < 3) return std::nullopt;
        std::string s = getStr(args[0]);
        uintptr_t dst = static_cast<uintptr_t>(getI64(args[1]));
        size_t maxLen = static_cast<size_t>(getI64(args[2]));
        if (!dst) return NirValue(static_cast<std::int64_t>(-1));
        size_t copyLen = std::min(s.size(), maxLen);
        if (copyLen > 0) std::memcpy(reinterpret_cast<void*>(dst), s.data(), copyLen);
        return NirValue(static_cast<std::int64_t>(copyLen));
    }
    if (name == "bytesToString") {
        if (args.size() < 2) return std::nullopt;
        uintptr_t src = static_cast<uintptr_t>(getI64(args[0]));
        size_t len = static_cast<size_t>(getI64(args[1]));
        if (!src && len > 0) return NirValue(std::string{});
        return NirValue(std::string(reinterpret_cast<const char*>(src), len));
    }
    if (name == "intToString") {
        if (args.empty()) return std::nullopt;
        return NirValue(std::to_string(getI64(args[0])));
    }

    for(size_t i=0;i<abi->service_count;++i){const auto& service=abi->services[i];if(service.name&&service.invoke&&name==service.name){std::vector<noqeri_abi_value> converted;converted.reserve(args.size());for(const auto& value:args)converted.push_back(toAbi(value));noqeri_abi_value result{};noqeri_abi_error abiError{};int32_t status=service.invoke(service.context?service.context:abi->context,converted.data(),converted.size(),&result,&abiError);if(status!=0){error=abiError.message?abiError.message:"Noqeri ABI service failed";return std::nullopt;}return fromAbi(result);}}
    return std::nullopt;
}
const noqeri_abi* defaultHostApi(){return defaultNoqeriAbi();}
std::optional<NirValue> callAbiFunction(const noqeri_abi* abi,const std::string& name,const std::vector<NirValue>& args,std::string& error){return callNoqeriAbi(abi,name,args,error);}
}
extern "C" uint32_t noqeri_abi_version(void){return NOQERI_ABI_VERSION;}
extern "C" int32_t noqeri_run_source(const char* source,const noqeri_abi* abi,noqeri_abi_error* error){if(!source){if(error){error->code=1;error->message="source must not be null";}return 1;}auto result=noe::compileSource(source);if(result.diagnostics.hasErrors()){if(error){error->code=2;error->message="source failed to compile";}return 2;}int status=noe::Interpreter(abi).run(result.nir);if(status!=0&&error){error->code=3;error->message="runtime execution failed";}return status==0?0:3;}
