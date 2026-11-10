// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
// Native bootstrap adapter for Noqeri's source-level package-manager frontend.
// No Node.js, shell invocation, dynamic code execution, or GitHub raw fallback.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <system_error>
#include <functional>
#ifdef NOQERI_PKG_WITH_CURL
#include <curl/curl.h>
#endif
#ifdef NOQERI_PKG_WITH_ZLIB
#include <zlib.h>
#endif

namespace noqeri_registry {
namespace fs = std::filesystem;
namespace {
constexpr size_t maxJson=2u*1024u*1024u, maxPackage=8u*1024u*1024u;
constexpr size_t maxExpand=9u*1024u*1024u, maxFiles=256;
[[noreturn]] void fail(const std::string& msg){throw std::runtime_error("NQR-PKG: " + msg);}

struct Json {
    enum Kind { Null, String, Object, Array, Bool, Number } kind=Null;
    std::string str;
    std::map<std::string,Json> obj;
    std::vector<Json> arr;
    bool truth=false;
    const Json& at(const std::string& field) const {
        if(kind!=Object)fail("expected JSON object for " + field);
        const auto it=obj.find(field); if(it==obj.end())fail("missing JSON field: " + field);
        return it->second;
    }
    const Json* find(const std::string& field) const {
        if(kind!=Object) return nullptr; auto it=obj.find(field);return it==obj.end()?nullptr:&it->second;
    }
    std::string string()const{if(kind!=String)fail("expected JSON string");return str;}
    bool boolean()const{if(kind!=Bool)fail("expected JSON bool");return truth;}
};
struct Parser {
    std::string_view data;size_t pos=0,depth=0;
    explicit Parser(std::string_view d):data(d){if(d.size()>maxJson)fail("JSON exceeds 2 MiB");}
    void ws(){while(pos<data.size() && (data[pos]==' '||data[pos]=='\n'||data[pos]=='\r'||data[pos]=='\t'))++pos;}
    char take(){if(pos>=data.size())fail("truncated JSON");return data[pos++];}
    void need(char x){ws();if(take()!=x)fail("malformed JSON");}
    void utf8(std::string&out,unsigned n){
        if(n<128)out.push_back(static_cast<char>(n));
        else if(n<2048){out.push_back(static_cast<char>(192|(n>>6)));out.push_back(static_cast<char>(128|(n&63)));}
        else if(n<65536){out.push_back(static_cast<char>(224|(n>>12)));out.push_back(static_cast<char>(128|((n>>6)&63)));out.push_back(static_cast<char>(128|(n&63)));}
        else {out.push_back(static_cast<char>(240|(n>>18)));out.push_back(static_cast<char>(128|((n>>12)&63)));out.push_back(static_cast<char>(128|((n>>6)&63)));out.push_back(static_cast<char>(128|(n&63)));}
    }
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;++i){char c=take();n*=16;if(c>='0'&&c<='9')n+=c-'0';else if(c>='a'&&c<='f')n+=c-'a'+10;else if(c>='A'&&c<='F')n+=c-'A'+10;else fail("invalid JSON unicode escape");}return n;}
    std::string string(){need('"');std::string s;while(true){auto c=take();if(c=='"')break;if(static_cast<unsigned char>(c)<32)fail("invalid JSON control byte");if(c!='\\'){s.push_back(c);continue;}
        c=take();switch(c){case '"':case '\\':case '/':s.push_back(c);break;case 'b':s.push_back('\b');break;case 'f':s.push_back('\f');break;case 'n':s.push_back('\n');break;case 'r':s.push_back('\r');break;case 't':s.push_back('\t');break;case 'u':{auto n=hex4();if(n>=0xd800&&n<=0xdbff){if(take()!='\\'||take()!='u')fail("bad surrogate pair");auto low=hex4();if(low<0xdc00||low>0xdfff)fail("bad surrogate pair");n=0x10000+((n-0xd800)<<10)+(low-0xdc00);}else if(n>=0xdc00&&n<=0xdfff)fail("lone low surrogate");utf8(s,n);break;}default:fail("invalid JSON escape");}
      }return s;
    }
    Json parse(){ws();if(++depth>32)fail("JSON nesting too deep");Json v;const char c=pos<data.size()?data[pos]:'\0';
      if(c=='"'){v.kind=Json::String;v.str=string();}
      else if(c=='{'){++pos;v.kind=Json::Object;ws();if(pos<data.size()&&data[pos]=='}')++pos;else {while(true){auto key=string();need(':');auto value=parse();if(!v.obj.emplace(key,std::move(value)).second)fail("duplicate JSON object key");ws();const auto delimiter=take();if(delimiter=='}')break;if(delimiter!=',')fail("malformed JSON object");}}}
      else if(c=='['){++pos;v.kind=Json::Array;ws();if(pos<data.size()&&data[pos]==']')++pos;else{while(true){v.arr.push_back(parse());ws();auto delimiter=take();if(delimiter==']')break;if(delimiter!=',')fail("malformed JSON array");}}}
      else if(data.substr(pos,4)=="true"){pos+=4;v.kind=Json::Bool;v.truth=true;}
      else if(data.substr(pos,5)=="false"){pos+=5;v.kind=Json::Bool;}
      else if(data.substr(pos,4)=="null"){pos+=4;v.kind=Json::Null;}
      else if(c=='-'||(c>='0'&&c<='9')){v.kind=Json::Number;size_t start=pos;while(pos<data.size()&&std::string_view("0123456789.-+eE").find(data[pos])!=std::string_view::npos)++pos;v.str=std::string(data.substr(start,pos-start));if(v.str.empty()||v.str.size()>32)fail("invalid JSON number");}
      else fail("unexpected JSON value");--depth;return v;
    }
    Json all(){auto v=parse();ws();if(pos!=data.size())fail("JSON trailing characters");return v;}
};
std::string quote(const std::string&value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out.push_back('\\');out.push_back(char(c));}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)fail("cannot encode control bytes");else out.push_back(char(c));}return out+'"';}

class Sha256 {
public:
    Sha256(){reset();}
    void update(const unsigned char*data,std::size_t len){for(std::size_t i=0;i<len;++i){buffer_[bufferLength_++]=data[i];if(bufferLength_==64){transform();bitLength_+=512;bufferLength_=0;}}}
    void update(std::string_view v){update(reinterpret_cast<const unsigned char*>(v.data()),v.size());}
    std::string finish(){std::size_t i=bufferLength_;buffer_[i++]=0x80;if(i>56){while(i<64)buffer_[i++]=0;transform();i=0;}while(i<56)buffer_[i++]=0;bitLength_+=static_cast<std::uint64_t>(bufferLength_)*8;for(int j=7;j>=0;--j)buffer_[i++]=static_cast<unsigned char>((bitLength_>>(j*8))&0xff);transform();std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:state_)out<<std::setw(8)<<word;reset();return out.str();}
private:
    static std::uint32_t rotr(std::uint32_t x,std::uint32_t n){return(x>>n)|(x<<(32-n));}
    void reset(){state_={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};bufferLength_=0;bitLength_=0;}
    void transform(){static constexpr std::array<std::uint32_t,64>k={0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};std::uint32_t w[64]{};for(std::size_t i=0;i<16;++i)w[i]=(static_cast<std::uint32_t>(buffer_[i*4])<<24)|(static_cast<std::uint32_t>(buffer_[i*4+1])<<16)|(static_cast<std::uint32_t>(buffer_[i*4+2])<<8)|buffer_[i*4+3];for(std::size_t i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];for(std::size_t i=0;i<64;++i){auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=h+s1+ch+k[i]+w[i];auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;}
    std::array<std::uint32_t,8>state_{};std::array<unsigned char,64>buffer_{};std::size_t bufferLength_=0;std::uint64_t bitLength_=0;
};
std::string digest(std::string_view data){Sha256 s;s.update(data);return "sha256:"+s.finish();}
std::string read(const fs::path&p,size_t limit){std::error_code ec;if(!fs::is_regular_file(p,ec)||fs::is_symlink(p,ec))fail("missing/unsafe file: "+p.string());const auto size=fs::file_size(p,ec);if(ec||size>limit)fail("file too large: "+p.string());std::ifstream in(p,std::ios::binary);if(!in)fail("cannot read "+p.string());std::string content(static_cast<size_t>(size),'\0');if(size && !in.read(&content[0],static_cast<std::streamsize>(size)))fail("cannot read "+p.string());return content;}
void write(const fs::path&p,std::string_view data){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary|std::ios::trunc);if(!out)fail("cannot create "+p.string());out.write(data.data(),static_cast<std::streamsize>(data.size()));if(!out.good())fail("failed writing "+p.string());}
void atomicText(const fs::path&p,std::string_view data){
    auto tmp=p;tmp+=".nqnew";auto backup=p;backup+=".nqbackup";
    if(fs::exists(tmp)||fs::is_symlink(tmp)||fs::exists(backup)||fs::is_symlink(backup))
        fail("unsafe existing transaction file for "+p.string());
    write(tmp,data);std::error_code ec;const bool had=fs::exists(p);
    if(had){fs::rename(p,backup,ec);if(ec){fs::remove(tmp);fail("cannot back up manifest: "+ec.message());}}
    fs::rename(tmp,p,ec);
    if(ec){std::error_code ignored;fs::remove(tmp,ignored);if(had)fs::rename(backup,p,ignored);fail("atomic manifest replacement failed: "+ec.message());}
    if(had)fs::remove(backup,ec);
}
std::string getenvText(const char*name){const char*v=std::getenv(name);return v&&*v?v:"";}
bool offline(){return getenvText("NOQERI_OFFLINE")=="1";}
fs::path home(){auto value=getenvText("NOQERI_HOME");if(value.empty()){auto h=getenvText("HOME");if(h.empty())h=getenvText("USERPROFILE");if(h.empty())fail("NOQERI_HOME or HOME required");return fs::path(h)/".noqeri";}return fs::path(value);}
bool identity(const std::string&value){static const std::regex re("^[a-z0-9][a-z0-9-]*$");return value.size()<=64&&std::regex_match(value,re);}
bool version(const std::string&value){static const std::regex re("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$");return value.size()<=100&&std::regex_match(value,re);}
bool integrity(const std::string&value){return value.size()==71&&value.compare(0,7,"sha256:")==0&&std::all_of(value.begin()+7,value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
struct Coordinate {std::string ns,name,ver;std::string full()const{return ns+"/"+name;} };
Coordinate parseCoordinate(const std::string&spec){auto slash=spec.find('/');auto at=spec.rfind('@');Coordinate c;if(slash==std::string::npos||slash==0)fail("package requires namespace/name[@version]");c.ns=spec.substr(0,slash);c.name=spec.substr(slash+1,at==std::string::npos?std::string::npos:at-slash-1);if(at!=std::string::npos)c.ver=spec.substr(at+1);if(!identity(c.ns)||!identity(c.name)||(!c.ver.empty()&&!version(c.ver)))fail("invalid package identity or version: "+spec);return c;}
std::string safePath(const std::string&name){if(name.empty()||name.size()>100||name[0]=='/'||name.find('\\')!=std::string::npos||name.find('\0')!=std::string::npos||name.find(':')!=std::string::npos)fail("unsafe archive path");std::stringstream s(name);std::string part;while(std::getline(s,part,'/'))if(part.empty()||part=="."||part=="..")fail("unsafe archive path component");if(name.back()=='/')fail("unsafe archive directory");return name;}
std::vector<unsigned> numericSegments(const std::string&v){std::vector<unsigned>out;std::stringstream s(v.substr(0,v.find('-')));std::string part;while(std::getline(s,part,'.')){try{out.push_back(static_cast<unsigned>(std::stoul(part)));}catch(...){fail("invalid version numeric segment");}}return out;}
int compareVersions(const std::string&a,const std::string&b){
    auto aa=numericSegments(a),bb=numericSegments(b);
    for(size_t i=0;i<3;++i)if(aa[i]!=bb[i])return aa[i]>bb[i]?1:-1;
    auto pre=[](const std::string&v){auto plus=v.find('+');auto dash=v.find('-');return dash==std::string::npos?std::string{}:v.substr(dash+1,(plus==std::string::npos?std::string::npos:plus-dash-1));};
    auto left=pre(a),right=pre(b);if(left.empty()!=right.empty())return left.empty()?1:-1;
    if(left==right)return 0;
    std::stringstream l(left),r(right);std::string lv,rv;
    while(true){bool hasL=bool(std::getline(l,lv,'.')),hasR=bool(std::getline(r,rv,'.'));if(!hasL||!hasR)return hasL?1:(hasR?-1:0);
        bool ln=!lv.empty()&&std::all_of(lv.begin(),lv.end(),[](unsigned char ch){return std::isdigit(ch)!=0;});
        bool rn=!rv.empty()&&std::all_of(rv.begin(),rv.end(),[](unsigned char ch){return std::isdigit(ch)!=0;});
        if(ln&&rn){if(lv.size()!=rv.size())return lv.size()>rv.size()?1:-1;}
        else if(ln!=rn)return ln?-1:1;
        if(lv!=rv)return lv>rv?1:-1;
    }
}
std::string registryUrl(){auto url=getenvText("NOQERI_REGISTRY_API_URL");if(url.empty())fail("set NOQERI_REGISTRY_API_URL to a Registry v1 HTTPS endpoint");if(url.find_first_of("?#\\ ")!=std::string::npos||url.find('@')!=std::string::npos)fail("invalid Registry URL");const bool https=url.rfind("https://",0)==0;const bool local=url.rfind("http://localhost",0)==0||url.rfind("http://127.0.0.1",0)==0||url.rfind("http://[::1]",0)==0;if(!https&&!local)fail("Registry requires HTTPS, except loopback HTTP for development");if(local){auto host=url.substr(7);auto end=host.find_first_of("/:");host=host.substr(0,end);if(host!="localhost"&&host!="127.0.0.1"&&url.rfind("http://[::1]",0)!=0)fail("HTTP registry must be loopback");}if(url.back()=='/')url.pop_back();return url;}
#ifdef NOQERI_PKG_WITH_CURL
size_t curlWrite(char*ptr,size_t size,size_t n,void*opaque){auto*out=static_cast<std::pair<std::string*,size_t>*>(opaque);if(size&&n>SIZE_MAX/size)return 0;auto bytes=size*n;if(out->first->size()>out->second||bytes>out->second-out->first->size())return 0;out->first->append(ptr,bytes);return bytes;}
#endif
std::string request(const std::string&path,size_t limit){if(offline())fail("offline mode forbids network access");
#ifdef NOQERI_PKG_WITH_CURL
    auto url=registryUrl()+path;CURL*curl=curl_easy_init();if(!curl)fail("native HTTP transport allocation failed");std::string body;std::pair<std::string*,size_t>sink{&body,limit};
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,12L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,curlWrite);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&sink);curl_easy_setopt(curl,CURLOPT_USERAGENT,"noqeri-native-registry/1");curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(curl,CURLOPT_MAXREDIRS,0L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif
    auto result=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(curl);
    if(result!=CURLE_OK||status!=200)fail("Registry request failed: "+path+" (HTTP "+std::to_string(status)+", curl "+std::to_string(result)+")");return body;
#else
    (void)path;(void)limit;fail("Noqeri SDK built without native HTTPS transport (libcurl); registry network operations unavailable");
#endif
}
std::string untarString(const unsigned char*data,size_t size){size_t n=0;while(n<size&&data[n])++n;return std::string(reinterpret_cast<const char*>(data),n);}
size_t octal(const unsigned char*d,size_t length){std::string raw=untarString(d,length);if(raw.empty())fail("invalid tar numeric field");size_t n=0;for(char c:raw){if(c==' ')continue;if(c<'0'||c>'7')fail("invalid tar octal field");if(n>(SIZE_MAX-7)/8)fail("tar octal field overflow");n=n*8+size_t(c-'0');}return n;}
struct Package {Coordinate id;std::string entry;std::string manifestName="package.noqeri",archiveIntegrity,integrityDigest;std::map<std::string,std::string> files;std::map<std::string,Coordinate> dependencies;fs::path path;};
std::map<std::string,Coordinate> manifestDeps(const std::string&input){std::map<std::string,Coordinate> deps;static const std::regex start("dependencies[[:space:]]*\\{");std::smatch match;if(!std::regex_search(input,match,start))return deps;auto pos=static_cast<size_t>(match.position()+match.length());const auto end=input.find('}',pos);if(end==std::string::npos)fail("unterminated package dependencies");const auto body=input.substr(pos,end-pos);static const std::regex dependency("([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*:[[:space:]]*\"([a-z0-9-]+/[a-z0-9-]+@[0-9A-Za-z.+-]+)\"");for(auto it=std::sregex_iterator(body.begin(),body.end(),dependency);it!=std::sregex_iterator();++it){auto alias=(*it)[1].str();auto dep=parseCoordinate((*it)[2].str());if(dep.ver.empty())fail("package dependency must pin version");if(!deps.emplace(alias,dep).second)fail("duplicate package dependency alias");}return deps;}
std::string manifestEntry(const std::string&text){static const std::regex re("\\bentry[[:space:]]*:[[:space:]]*\"([^\"]+)\"");std::smatch m;if(!std::regex_search(text,m,re))fail("package manifest missing entry");const auto entry=safePath(m[1]);if(entry.size()<7||entry.substr(entry.size()-7)!=".noqeri")fail("package entry must be .noqeri");return entry;}
std::string packageDigest(const std::map<std::string,std::string>&files){Sha256 sha;const unsigned char zero=0;for(const auto& [name,bytes]:files){sha.update(name);sha.update(&zero,1);sha.update(std::string_view(bytes));sha.update(&zero,1);}return "sha256:"+sha.finish();}
std::map<std::string,std::string> extract(std::string_view archive,const std::string&expected){if(!integrity(expected)||digest(archive)!=expected)fail("archive SHA-256 mismatch");if(archive.size()>maxPackage)fail("compressed package too large");
#ifdef NOQERI_PKG_WITH_ZLIB
    z_stream z{};if(inflateInit2(&z,MAX_WBITS+16)!=Z_OK)fail("zlib inflate initialization failed");std::string expanded;std::array<char,32768> buffer{};z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(archive.data()));z.avail_in=static_cast<uInt>(archive.size());int code=Z_OK;while(code==Z_OK){z.next_out=reinterpret_cast<Bytef*>(buffer.data());z.avail_out=static_cast<uInt>(buffer.size());code=inflate(&z,Z_NO_FLUSH);const size_t produced=buffer.size()-z.avail_out;if(expanded.size()+produced>maxExpand){inflateEnd(&z);fail("decompressed package exceeds limit");}expanded.append(buffer.data(),produced);}const bool good=code==Z_STREAM_END&&z.avail_in==0;inflateEnd(&z);if(!good)fail("invalid gzip archive or trailing data");
    std::map<std::string,std::string> files;size_t offset=0,total=0;bool ended=false;while(offset<expanded.size()){if(expanded.size()-offset<512)fail("truncated tar header");const auto*head=reinterpret_cast<const unsigned char*>(expanded.data()+offset);offset+=512;bool zero=true;for(size_t i=0;i<512;++i)if(head[i]){zero=false;break;}if(zero){ended=true;break;}if(std::memcmp(head+257,"ustar\0",6)!=0)fail("unsupported tar format");for(size_t i=345;i<500;++i)if(head[i])fail("tar prefix paths are not supported");if(head[156]!=0&&head[156]!='0')fail("tar archive contains links or special files");auto name=safePath(untarString(head,100));auto size=octal(head+124,12);auto sum=octal(head+148,8);size_t checksum=0;for(size_t i=0;i<512;++i)checksum+=(i>=148&&i<156)?32:head[i];if(checksum!=sum)fail("invalid tar checksum");if(size>maxPackage-total||files.size()>=maxFiles)fail("package size/file limit exceeded");total+=size;size_t padded=(size+511)/512*512;if(padded>expanded.size()-offset)fail("truncated tar member");if(!files.emplace(name,expanded.substr(offset,size)).second)fail("duplicate tar member");offset+=padded;}
    if(!ended||expanded.size()%512!=0||!std::all_of(expanded.begin()+static_cast<std::ptrdiff_t>(offset),expanded.end(),[](char c){return c==0;}))fail("invalid tar terminator");return files;
#else
    (void)archive;fail("Noqeri SDK built without native gzip (zlib); registry archives unavailable");
#endif
}
std::string meta(const Package&p){std::ostringstream os;size_t count=0,bytes=0;for(const auto&f:p.files){++count;bytes+=f.second.size();}os<<"{\n  \"coordinate\": "<<quote(p.id.full())<<",\n  \"version\": "<<quote(p.id.ver)<<",\n  \"entry\": "<<quote(p.entry)<<",\n  \"manifestName\": \"package.noqeri\",\n  \"integrity\": "<<quote(p.integrityDigest)<<",\n  \"archiveIntegrity\": "<<quote(p.archiveIntegrity)<<",\n  \"files\": "<<count<<",\n  \"bytes\": "<<bytes<<"\n}\n";return os.str();}
fs::path cachePath(const Coordinate&c){return home()/"cache/registry-v1"/c.ns/c.name/c.ver;}
Package fromCache(const Coordinate&c){Package p;p.id=c;p.path=cachePath(c);std::error_code ec;if(!fs::is_directory(p.path,ec)||fs::is_symlink(p.path,ec))fail("missing or unsafe cached package: "+c.full()+"@"+c.ver);const auto metadata=Parser(read(p.path/".noqeri-integrity.json",maxJson)).all();if(metadata.at("coordinate").string()!=c.full()||metadata.at("version").string()!=c.ver)fail("cached package metadata identity mismatch");p.entry=safePath(metadata.at("entry").string());p.integrityDigest=metadata.at("integrity").string();if(!integrity(p.integrityDigest))fail("invalid cached package integrity");if(const auto*x=metadata.find("archiveIntegrity"))p.archiveIntegrity=x->string();size_t bytes=0;for(auto it=fs::recursive_directory_iterator(p.path,fs::directory_options::none);it!=fs::recursive_directory_iterator();++it){if(it->is_symlink()||(!it->is_directory()&&!it->is_regular_file()))fail("cached package includes unsafe file/link");if(it->is_directory())continue;const auto rel=fs::relative(it->path(),p.path).generic_string();if(rel==".noqeri-integrity.json")continue;safePath(rel);if(p.files.size()>=maxFiles)fail("cached package file limit exceeded");const auto text=read(it->path(),maxPackage-bytes);bytes+=text.size();if(bytes>maxPackage)fail("cached package byte limit exceeded");p.files.emplace(rel,text);}
    if(p.files.find("package.noqeri")==p.files.end()||p.files.find(p.entry)==p.files.end()||packageDigest(p.files)!=p.integrityDigest)fail("cached package integrity mismatch: "+c.full());
    const auto* fileCount=metadata.find("files");const auto* byteCount=metadata.find("bytes");
    if(!fileCount||!byteCount||fileCount->kind!=Json::Number||byteCount->kind!=Json::Number||
       fileCount->str!=std::to_string(p.files.size())||byteCount->str!=std::to_string(bytes))
       fail("cached package metadata file-count/byte mismatch");
    if(manifestEntry(p.files.at("package.noqeri"))!=p.entry)fail("cached package manifest entry mismatch");p.dependencies=manifestDeps(p.files.at("package.noqeri"));return p;}
void saveCache(Package&p){const auto target=cachePath(p.id);auto dir=target;dir+=".tmp";if(fs::exists(dir)||fs::is_symlink(dir))fail("unsafe temporary registry cache path");fs::create_directories(dir);try{for(const auto&[name,data]:p.files)write(dir/safePath(name),data);write(dir/".noqeri-integrity.json",meta(p));fs::create_directories(target.parent_path());if(fs::exists(target)||fs::is_symlink(target))fail("existing corrupt package cache; remove it before retrying: "+target.string());fs::rename(dir,target);p.path=target;}catch(...){std::error_code ec;fs::remove_all(dir,ec);throw;}}
std::string selectVersion(const Json&index,const Coordinate&c){const auto&list=index.at("packages");if(list.kind!=Json::Array)fail("registry index packages must be array");std::string best;for(const auto&entry:list.arr){if(entry.at("namespace").string()!=c.ns||entry.at("name").string()!=c.name)continue;const auto&vs=entry.at("versions");if(vs.kind!=Json::Array)fail("invalid index versions");for(const auto&v:vs.arr){const auto x=v.at("version").string();if(!version(x)||v.at("yanked").boolean()||(!c.ver.empty()&&x!=c.ver))continue;if(best.empty()||compareVersions(x,best)>0)best=x;}break;}if(best.empty())fail("no usable release for "+c.full()+(c.ver.empty()?"":"@"+c.ver));return best;}
Package download(Coordinate c){if(offline())fail("package not cached (offline): "+c.full());const auto idx=Parser(request("/v1/index",maxJson)).all();if(idx.at("protocol").string()!="v1"||idx.at("packageFormat").string()!="nqpkg-1")fail("unsupported Registry protocol/format");c.ver=selectVersion(idx,c);try{return fromCache(c);}catch(const std::exception&){if(fs::exists(cachePath(c)))throw;}
    const std::string endpoint="/v1/packages/"+c.ns+"/"+c.name+"/"+c.ver;const auto m=Parser(request(endpoint,maxJson)).all();if(m.at("version").string()!=c.ver||m.at("yanked").boolean())fail("registry metadata identity/yank mismatch");Package p;p.id=c;p.entry=safePath(m.at("entry").string());p.archiveIntegrity=m.at("archiveIntegrity").string();if(!integrity(p.archiveIntegrity))fail("invalid registry archive integrity");p.files=extract(request(endpoint+"/download",maxPackage),p.archiveIntegrity);if(!p.files.count("package.noqeri"))fail("package.noqeri missing");if(manifestEntry(p.files.at("package.noqeri"))!=p.entry||!p.files.count(p.entry))fail("Registry entry disagrees with archive");p.integrityDigest=packageDigest(p.files);p.dependencies=manifestDeps(p.files.at("package.noqeri"));saveCache(p);return p;}
Package get(Coordinate c){if(!c.ver.empty()){try{return fromCache(c);}catch(const std::exception&){if(fs::exists(cachePath(c)))throw;if(offline())throw;}}if(offline()){auto base=home()/"cache/registry-v1"/c.ns/c.name;std::error_code ec;if(!fs::is_directory(base,ec))fail("offline package not cached: "+c.full());for(const auto&item:fs::directory_iterator(base)){if(item.is_directory()&&version(item.path().filename().string())){auto ver=item.path().filename().string();if(c.ver.empty()||compareVersions(ver,c.ver)>0)c.ver=ver;}}if(c.ver.empty())fail("offline package not cached: "+c.full());return fromCache(c);}return download(c);}
fs::path projectFile(fs::path path=fs::current_path()){std::error_code ec;if(fs::is_regular_file(path,ec))path=path.parent_path();path=fs::absolute(path);while(true){for(const char*file:{"project.nqon","project.noqeri"})if(fs::is_regular_file(path/file,ec))return path/file;auto parent=path.parent_path();if(parent==path||parent.empty())fail("project.nqon not found; run noqeri new <name>");path=parent;}}
std::map<std::string,Coordinate> projectDeps(const fs::path&path,const std::string&source){std::map<std::string,Coordinate>deps;const bool nqon=path.extension()==".nqon";auto opening=source.find("packages {");if(opening==std::string::npos)opening=source.find("dependencies {");if(opening==std::string::npos)return deps;auto from=source.find('{',opening),to=source.find('}',from);if(to==std::string::npos)fail("unterminated project dependencies block");auto body=source.substr(from+1,to-from-1);std::regex line(nqon?"([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*\"([a-z0-9-]+/[a-z0-9-]+@[0-9A-Za-z.+-]+)\"":"([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*:[[:space:]]*\"([a-z0-9-]+/[a-z0-9-]+@[0-9A-Za-z.+-]+)\"");for(auto it=std::sregex_iterator(body.begin(),body.end(),line);it!=std::sregex_iterator();++it){const auto alias=(*it)[1].str();auto c=parseCoordinate((*it)[2].str());if(c.ver.empty())fail("dependency needs pinned version");if(!deps.emplace(alias,c).second)fail("duplicate project dependency alias");}return deps;}
std::string modifyProject(const fs::path&path,std::string source,const Coordinate&c){const bool nqon=path.extension()==".nqon";auto opening=source.find("packages {");if(opening==std::string::npos)opening=source.find("dependencies {");std::string alias=c.name;std::replace(alias.begin(),alias.end(),'-','_');const std::string line = "    " + alias + (nqon ? " = \"" : " : \"") + c.full() + "@" + c.ver + "\"\n";if(opening==std::string::npos){if(!source.empty()&&source.back()!='\n')source+='\n';return source+"\npackages {\n"+line+"}\n";}const auto from=source.find('{',opening),end=source.find('}',from);if(end==std::string::npos)fail("unterminated package block");auto body=source.substr(from+1,end-from-1);auto deps=projectDeps(path,source);for(const auto&[key,item]:deps)if(key==alias&&item.full()!=c.full())fail("package alias conflict: "+alias);static const std::regex token("([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*[:=][[:space:]]*\"[a-z0-9-]+/[a-z0-9-]+@[0-9A-Za-z.+-]+\"");std::string rebuilt;auto cur=body.cbegin();for(auto it=std::sregex_iterator(body.begin(),body.end(),token);it!=std::sregex_iterator();++it){auto match=*it;rebuilt.append(cur,body.cbegin()+match.position());if(match[1].str()!=alias)rebuilt+=match.str();else rebuilt+=line.substr(0,line.size()-1);cur=body.cbegin()+match.position()+match.length();}rebuilt.append(cur,body.cend());if(!deps.count(alias))rebuilt+="\n"+line;return source.substr(0,from+1)+rebuilt+source.substr(end);}
struct Graph {std::map<std::string,Package> items;std::set<std::string> visiting;void visit(const Coordinate&c,int depth=0){if(depth>32||items.size()>128)fail("dependency graph limit exceeded");auto it=items.find(c.full());if(it!=items.end()){if(!c.ver.empty()&&c.ver!=it->second.id.ver)fail("dependency conflict "+c.full());return;}if(!visiting.insert(c.full()).second)fail("dependency cycle at "+c.full());auto p=get(c);for(const auto&[alias,dep]:p.dependencies){(void)alias;visit(dep,depth+1);}visiting.erase(c.full());items.emplace(c.full(),std::move(p));}};
std::string lock(const Graph&g){std::string out="noqeri-lock 3\n";for(const auto&[name,p]:g.items)out+="package "+p.id.name+" "+name+" "+p.id.ver+" "+p.integrityDigest+" "+p.entry+"\n";return out;}
void install(const fs::path&project,std::string src){auto deps=projectDeps(project,src);Graph g;for(const auto&[alias,c]:deps){(void)alias;g.visit(c);}atomicText(project.parent_path()/"noqeri.lock",lock(g));std::cout<<"installed "<<g.items.size()<<" verified Noqeri package(s)\n";}
} // namespace

void operation(const std::string&cmd,const std::vector<std::string>&args){
  if(cmd=="help"||cmd.empty()){
    std::cout<<"Noqeri native Registry v1 package manager (no Node.js)\n"
             <<"  noqeri pkg add namespace/name[@version]\n  noqeri pkg install\n"
             <<"  noqeri pkg resolve source.noqeri\n  noqeri pkg vendor\n"
             <<"  noqeri pkg audit\n  noqeri pkg help\n"
             <<"Online registry: NOQERI_REGISTRY_API_URL=https://...\nOffline: NOQERI_OFFLINE=1\n";
    return;
  }
  if(cmd=="add"){
    if(args.size()!=1)fail("add requires namespace/name[@version]");auto p=get(parseCoordinate(args[0]));auto project=projectFile();auto src=read(project,maxJson);auto next=modifyProject(project,src,p.id);auto deps=projectDeps(project,next);Graph graph;for(const auto&[alias,c]:deps){(void)alias;graph.visit(c);}atomicText(project,next);atomicText(project.parent_path()/"noqeri.lock",lock(graph));std::cout<<"added "<<p.id.full()<<"@"<<p.id.ver<<"\n";return;
  }
  if(cmd=="install"){
    if(!args.empty())fail("install takes no arguments");auto project=projectFile();install(project,read(project,maxJson));return;
  }
  if(cmd=="resolve"){
    if(args.size()!=1)fail("resolve requires source.noqeri");auto source=fs::absolute(args[0]);auto project=projectFile(source.parent_path());auto deps=projectDeps(project,read(project,maxJson));auto code=read(source,maxJson);std::regex import("import[[:space:]]+package[[:space:]]+\"([a-z0-9-]+/[a-z0-9-]+)\"");std::set<std::string> visited;for(auto it=std::sregex_iterator(code.begin(),code.end(),import);it!=std::sregex_iterator();++it){auto name=(*it)[1].str();if(!visited.insert(name).second)continue;bool found=false;for(const auto&[alias,c]:deps){(void)alias;if(c.full()!=name)continue;auto pkg=get(c);std::cout<<name<<"@"<<pkg.id.ver<<" -> "<<(pkg.path/pkg.entry)<<" ("<<pkg.integrityDigest<<")\n";found=true;break;}if(!found)fail("undeclared package import: "+name);}return;
  }
  if(cmd=="vendor"){
    if(!args.empty()&&!(args.size()==1&&args[0]=="--offline"))fail("vendor accepts only --offline");auto project=projectFile();auto deps=projectDeps(project,read(project,maxJson));Graph g;for(const auto&[alias,c]:deps){(void)alias;g.visit(c);}auto root=project.parent_path()/".noqeri/vendor-home/cache/registry-v1";for(const auto&[key,p]:g.items){(void)key;auto dest=root/p.id.ns/p.id.name/p.id.ver;if(fs::exists(dest)||fs::is_symlink(dest))fail("vendor target already exists: "+dest.string());fs::create_directories(dest.parent_path());fs::copy(p.path,dest,fs::copy_options::recursive);}std::cout<<"vendored "<<g.items.size()<<" packages to .noqeri/vendor-home\n";return;
  }
  if(cmd=="audit"){
    if(!args.empty()&&!(args.size()==1&&args[0]=="--offline"))fail("audit accepts only --offline");auto project=projectFile();auto deps=projectDeps(project,read(project,maxJson));Graph g;for(const auto&[alias,c]:deps){(void)alias;g.visit(c);}if(offline()||(!args.empty()&&args[0]=="--offline"))fail("advisory catalog is unavailable offline; refusing to claim success");auto advisories=Parser(request("/v1/advisories",maxJson)).all();const auto&arr=advisories.at("advisories");if(arr.kind!=Json::Array)fail("invalid registry advisory catalog");if(!arr.arr.empty())fail("advisories present; manual review required (fail closed)");std::cout<<"audit passed: "<<g.items.size()<<" verified packages; registry advisory catalog empty\n";return;
  }
  fail("unknown package command: "+cmd);
}
} // namespace noqeri_registry

#ifdef NOQERI_REGISTRY_STANDALONE
int main(int argc,char**argv){try{std::string command=argc>1?argv[1]:"help";std::vector<std::string>args;for(int i=2;i<argc;++i)args.emplace_back(argv[i]);noqeri_registry::operation(command,args);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
#endif
