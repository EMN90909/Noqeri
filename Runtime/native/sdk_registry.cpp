// SPDX-License-Identifier: Apache-2.0
// Native bootstrap adapter for the Noqeri registry. No Node.js or JavaScript
// runtime: HTTPS and SHA-256 are provided by platform-native libraries.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(NOQERI_REGISTRY_NETWORK)
#include <curl/curl.h>
#include <openssl/sha.h>
#endif
namespace fs=std::filesystem;
namespace {
void fail(const std::string& message){throw std::runtime_error(message);}
std::string env(const char* name,const std::string& fallback=""){const char* v=std::getenv(name);return v&&*v?v:fallback;}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)fail("cannot read "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& p,const std::string& value){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!f)fail("cannot write "+p.string());f.write(value.data(),static_cast<std::streamsize>(value.size()));if(!f)fail("write failed "+p.string());}
bool valid(const std::string& s,const std::string& pattern){return std::regex_match(s,std::regex(pattern));}
void safePath(const std::string& s){
 if(s.empty()||s[0]=='/'||s.find('\\')!=std::string::npos||s.find('\0')!=std::string::npos)fail("unsafe package path");
 for(const auto& part:fs::path(s))if(part=="."||part==".."||part.empty())fail("unsafe package path");
}
std::string escaped(const std::string& value){std::string out="\"";for(char c:value){if(c=='\"'||c=='\\')out+='\\';if(c=='\n'){out+="\\n";continue;}out+=c;}return out+"\"";}
struct Json{
 std::string scalar;std::vector<Json> array;std::map<std::string,Json> object;
 const Json& at(const std::string& k)const{auto i=object.find(k);if(i==object.end())fail("missing registry field: "+k);return i->second;}
 std::string str()const{return scalar;}
 bool boolean()const{return scalar=="true";}
};
class Parser{
 const std::string& s;size_t i=0;
 void ws(){while(i<s.size()&&std::isspace(static_cast<unsigned char>(s[i])))++i;}
 char get(){ws();if(i>=s.size())fail("truncated JSON");return s[i++];}
 std::string string(){
  if(get()!='"')fail("expected JSON string");std::string o;
  while(i<s.size()){char c=s[i++];if(c=='"')return o;if(c=='\\'){if(i>=s.size())fail("invalid escape");char e=s[i++];if(e=='n')o+='\n';else if(e=='r')o+='\r';else if(e=='t')o+='\t';else if(e=='"'||e=='\\'||e=='/')o+=e;else if(e=='u'){if(i+4>s.size())fail("invalid unicode escape"); // Avoid ambiguous path decoding.
    fail("unicode escapes are not supported in registry path fields");
   }else fail("unsupported JSON escape");}else o+=c;}
  fail("unterminated JSON string");return {};
 }
 public: explicit Parser(const std::string& t):s(t){}
 Json value(int depth=0){if(depth>48)fail("JSON nesting exceeds limit");ws();if(i>=s.size())fail("truncated JSON");
  Json v;char c=s[i];if(c=='"'){v.scalar=string();return v;}
  if(c=='{'){++i;ws();if(i<s.size()&&s[i]=='}'){++i;return v;}for(;;){ws();auto k=string();if(get()!=':')fail("expected colon");if(!v.object.emplace(k,value(depth+1)).second)fail("duplicate JSON key");char e=get();if(e=='}')break;if(e!=',')fail("invalid JSON object");}return v;}
  if(c=='['){++i;ws();if(i<s.size()&&s[i]==']'){++i;return v;}for(;;){v.array.push_back(value(depth+1));char e=get();if(e==']')break;if(e!=',')fail("invalid JSON array");}return v;}
  size_t start=i;while(i<s.size()&&(std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='.'||s[i]=='-'||s[i]=='+'))++i;
  if(start==i)fail("invalid JSON token");v.scalar=s.substr(start,i-start);return v;
 }
 Json parse(){auto v=value();ws();if(i!=s.size())fail("trailing JSON data");return v;}
};
bool offline(){return env("NOQERI_OFFLINE")=="1";}
fs::path home(){
 auto configured=env("NOQERI_HOME");if(!configured.empty())return fs::absolute(configured);
#ifdef _WIN32
 return fs::path(env("USERPROFILE","."))/".noqeri";
#else
 return fs::path(env("HOME","."))/".noqeri";
#endif
}
#if defined(NOQERI_REGISTRY_NETWORK)
size_t receive(char* p,size_t a,size_t b,void* data){auto* out=static_cast<std::string*>(data);size_t n=a*b;if(n>8*1024*1024-out->size())return 0;out->append(p,n);return n;}
std::string get(const std::string& url,size_t limit=2097152){
 if(offline())fail("offline mode forbids network access");
 if(url.rfind("https://",0)!=0&&url.rfind("http://127.0.0.1/",0)!=0&&url.rfind("http://localhost/",0)!=0)
   fail("registry URLs require HTTPS (localhost permitted for tests)");
 CURL* curl=curl_easy_init();if(!curl)fail("libcurl initialization failed");
 std::string data;curl_easy_setopt(curl,CURLOPT_URL,url.c_str());curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
 curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS|CURLPROTO_HTTP);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
 curl_easy_setopt(curl,CURLOPT_TIMEOUT,12L);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
 curl_easy_setopt(curl,CURLOPT_WRITEDATA,&data);curl_easy_setopt(curl,CURLOPT_USERAGENT,"noqeri-native-registry/1");
 auto result=curl_easy_perform(curl);long code=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
 curl_easy_cleanup(curl);if(result!=CURLE_OK||code!=200||data.size()>limit)fail("registry request failed: "+url);
 return data;
}
std::string hashFiles(const std::map<std::string,std::string>& files){
 SHA256_CTX c;SHA256_Init(&c);for(const auto& f:files){SHA256_Update(&c,f.first.data(),f.first.size());
 const char nul=0;SHA256_Update(&c,&nul,1);SHA256_Update(&c,f.second.data(),f.second.size());SHA256_Update(&c,&nul,1);}
 unsigned char hash[SHA256_DIGEST_LENGTH];SHA256_Final(hash,&c);
 std::ostringstream out;out<<"sha256:"<<std::hex<<std::setfill('0');
 for(unsigned char b:hash)out<<std::setw(2)<<static_cast<unsigned int>(b);return out.str();
}
#else
std::string get(const std::string&,size_t=2097152){fail("native HTTPS/SHA support unavailable: rebuild SDK with libcurl and OpenSSL");return {};}
std::string hashFiles(const std::map<std::string,std::string>&){fail("native SHA-256 unavailable: rebuild with OpenSSL");return {};}
#endif
const std::string coordinateRx="[A-Za-z0-9_-]+/[A-Za-z0-9_-]+";
const std::string versionRx="[0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z.+-]+)?";
std::pair<std::string,std::string> splitSpec(std::string s){auto a=s.rfind('@');std::string version=a==std::string::npos?"":s.substr(a+1);if(a!=std::string::npos)s.resize(a);
 if(!valid(s,coordinateRx)||(!version.empty()&&!valid(version,versionRx)))fail("expected namespace/name[@semantic-version]");return {s,version};}
std::string alias(const std::string& coordinate){auto a=coordinate.substr(coordinate.find('/')+1);std::replace(a.begin(),a.end(),'-','_');return a;}
std::string versionKey(const std::string& v){std::ostringstream o;std::istringstream in(v);std::string n;for(int i=0;i<3;++i){std::getline(in,n,'.');o<<std::setw(12)<<std::setfill('0')<<std::stoull(n);}return o.str()+((v.find('-')==std::string::npos)?"z":"a"+v);}
std::string indexUrl(){return env("NOQERI_REGISTRY_INDEX","https://raw.githubusercontent.com/EMN90909/noqeri-registry/main/registry/index.json");}
std::string contentBase(){return env("NOQERI_REGISTRY_CONTENTS","https://api.github.com/repos/EMN90909/noqeri-registry/contents/");}
struct Release{std::string version,path,integrity;};
Release release(const std::string& coordinate,const std::string& requested){
 const auto [ns,name]=splitSpec(coordinate);(void)ns;(void)name;
 auto index=Parser(get(indexUrl())).parse();std::string best,base,integrity;
 for(auto& pkg:index.at("packages").array){
  if(pkg.at("namespace").str()+"/"+pkg.at("name").str()!=coordinate)continue;
  for(auto& ver:pkg.at("versions").array){
   std::string v=ver.at("version").str();if(!valid(v,versionRx)||(!requested.empty()&&v!=requested))continue;
   auto yank=ver.object.find("yanked");if(requested.empty()&&yank!=ver.object.end()&&yank->second.boolean())continue;
   if(best.empty()||versionKey(v)>versionKey(best)){best=v;base=ver.at("path").str();auto it=ver.object.find("integrity");integrity=it==ver.object.end()?"":it->second.str();}
  }
 }
 if(best.empty())fail("registry package/version not found: "+coordinate+"@"+requested);
 safePath(base);if(!integrity.empty()&&!valid(integrity,"sha256:[0-9a-f]{64}"))fail("invalid published package SHA256");return {best,base,integrity};
}
fs::path packagePath(const std::string& coord,const std::string& version){return home()/"cache/registry-v1"/coord/version;}
std::map<std::string,std::string> localFiles(const fs::path& dir){
 std::map<std::string,std::string> files;size_t bytes=0;
 for(auto& f:fs::recursive_directory_iterator(dir,fs::directory_options::none)){
  if(f.is_symlink()||(!f.is_directory()&&!f.is_regular_file()))fail("unsafe cached package entry");
  if(!f.is_regular_file()||f.path().filename()==".noqeri-integrity.json")continue;
  auto key=fs::relative(f.path(),dir).generic_string();safePath(key);
  auto data=read(f.path());bytes+=data.size();if(bytes>8388608||files.size()>=256)fail("package limits exceeded");
  files.emplace(key,std::move(data));
 }return files;
}
struct Package{std::string coordinate,version,integrity,entry;fs::path path;};
Package verify(const std::string& coord,const std::string& version){
 auto dir=packagePath(coord,version);auto meta=Parser(read(dir/".noqeri-integrity.json")).parse();
 if(meta.at("coordinate").str()!=coord||meta.at("version").str()!=version)fail("cached package identity mismatch");
 auto files=localFiles(dir);auto digest=hashFiles(files);if(digest!=meta.at("integrity").str())fail("cached package SHA-256 mismatch");
 auto entry=meta.at("entry").str();safePath(entry);if(!files.count(entry))fail("cached package entry missing");
 return {coord,version,digest,entry,dir};
}
void listFiles(const std::string& base,const std::string& path,std::map<std::string,std::string>& files,size_t& bytes,int depth){
 if(depth>16)fail("registry directory nesting too deep");
 auto items=Parser(get(contentBase()+path+"?ref=main")).parse();
 for(auto& item:items.array){auto type=item.at("type").str(),p=item.at("path").str();
  if(p.rfind(base+"/",0)!=0)fail("registry path escaped package root");
  auto rel=p.substr(base.size()+1);safePath(rel);
  if(type=="dir"){listFiles(base,p,files,bytes,depth+1);continue;}if(type!="file")continue;
  if(files.size()>=256)fail("package file count exceeded");
  auto data=get(item.at("download_url").str(),8388608-bytes);
  bytes+=data.size();if(bytes>8388608||!files.emplace(rel,std::move(data)).second)fail("duplicate/oversized registry file");
 }
}
Package fetch(const std::string& coord,std::string requested){
 if(!requested.empty()){try{return verify(coord,requested);}catch(const std::exception&){if(offline())throw;}}
 if(offline()){
  auto root=packagePath(coord,"");std::string best;
  if(!fs::exists(root))fail("package not cached: "+coord);
  for(auto& e:fs::directory_iterator(root)){auto v=e.path().filename().string();
   if(e.is_directory()&&valid(v,versionRx)&&(best.empty()||versionKey(v)>versionKey(best)))best=v;}
  if(best.empty())fail("package not cached: "+coord);return verify(coord,best);
 }
 auto [version,base,expectedHash]=release(coord,requested);
 try{return verify(coord,version);}catch(const std::exception&){}
 std::map<std::string,std::string> files;size_t bytes=0;listFiles(base,base,files,bytes,0);
 if(!files.count("package.noqeri"))fail("registry package manifest missing");
 auto entry=std::regex(R"re(\bentry\s*:\s*"([^"]+)")re");
 std::smatch match;if(!std::regex_search(files.at("package.noqeri"),match,entry))fail("package entry missing");
 auto relative=match[1].str();safePath(relative);if(!files.count(relative))fail("package entry file missing");
 auto digest=hashFiles(files);if(!expectedHash.empty()&&digest!=expectedHash)fail("published registry SHA256 mismatch");auto target=packagePath(coord,version);
 // Only accept immutable registry hashes when declared; never execute package code.
 auto staging=fs::path(target.string()+".native-temp");fs::remove_all(staging);fs::create_directories(staging);
 try{
  for(auto& f:files)write(staging/f.first,f.second);
  std::ostringstream meta;meta<<"{\n  \"coordinate\": "<<escaped(coord)<<",\n  \"version\": "<<escaped(version)
  <<",\n  \"integrity\": "<<escaped(digest)<<",\n  \"entry\": "<<escaped(relative)
  <<",\n  \"files\": "<<files.size()<<",\n  \"bytes\": "<<bytes<<"\n}\n";
  write(staging/".noqeri-integrity.json",meta.str());fs::remove_all(target);fs::rename(staging,target);
 }catch(...){fs::remove_all(staging);throw;}
 return verify(coord,version);
}
fs::path findProject(fs::path dir=fs::current_path()){
 dir=fs::absolute(dir);if(fs::is_regular_file(dir))dir=dir.parent_path();
 for(;;){for(auto name:{"project.nqon","project.noqeri"})if(fs::is_regular_file(dir/name))return dir/name;
 auto p=dir.parent_path();if(p==dir)fail("cannot find project.nqon or project.noqeri");dir=p;}
}
struct Dep{std::string alias,coordinate,version,integrity;};
std::vector<Dep> dependencies(const std::string& source,const std::string& project){
 std::vector<Dep> out;
 auto block=source.find(project=="project.nqon"?"packages":"dependencies");
 if(block==std::string::npos)return out;
 auto open=source.find('{',block);auto end=source.find('}',open);
 if(open==std::string::npos||end==std::string::npos)fail("invalid package dependency block");
 const auto body=source.substr(open+1,end-open-1);
 const std::regex rx(project=="project.nqon"?R"re(([A-Za-z_][A-Za-z0-9_-]*)\s*=\s*"?([A-Za-z0-9_/-]+@[0-9A-Za-z.+-]+)"?)re":R"re(([A-Za-z_][A-Za-z0-9_]*)\s*:\s*"([^"]+)")re");
 for(auto p=std::sregex_iterator(body.begin(),body.end(),rx);p!=std::sregex_iterator();++p){auto [coord,ver]=splitSpec((*p)[2].str());if(ver.empty())fail("dependency version must be pinned");out.push_back({(*p)[1].str(),coord,ver});}
 return out;
}
std::vector<Dep> packageDeps(const Package& p){return dependencies(read(p.path/"package.noqeri"),"package.noqeri");}
std::vector<Package> install(const fs::path& manifest){
 auto deps=dependencies(read(manifest),manifest.filename().string());std::map<std::string,Package> done;std::set<std::string> visiting;
 std::function<void(Dep,int)> visit=[&](Dep d,int depth){
  if(depth>32)fail("dependency tree too deep");auto prior=done.find(d.coordinate);
  if(prior!=done.end()){if(prior->second.version!=d.version)fail("package version conflict: "+d.coordinate);return;}
  if(!visiting.insert(d.coordinate).second)fail("dependency cycle: "+d.coordinate);
  auto pkg=fetch(d.coordinate,d.version);done.emplace(d.coordinate,pkg);
  for(auto& child:packageDeps(pkg))visit(child,depth+1);
  visiting.erase(d.coordinate);std::cout<<"installed "<<d.coordinate<<"@"<<pkg.version<<"\n";
 };
 for(auto& dep:deps)visit(dep,0);
 std::ostringstream lock;lock<<"noqeri-lock 3\n";for(auto& item:done)
  lock<<"package "<<alias(item.first)<<" "<<item.first<<" "<<item.second.version<<" "<<item.second.integrity<<" "<<item.second.entry<<"\n";
 write(manifest.parent_path()/"noqeri.lock",lock.str());
 std::vector<Package> result;for(auto& p:done)result.push_back(p.second);return result;
}
std::vector<Dep> lockDeps(const fs::path& file){
 std::istringstream in(read(file));std::string line;std::getline(in,line);if(line!="noqeri-lock 3")fail("expected noqeri-lock 3");
 std::vector<Dep> deps;
 while(std::getline(in,line)){std::istringstream row(line);std::string tag,a,c,v,integrity,entry;
 if(!(row>>tag>>a>>c>>v>>integrity>>entry)||tag!="package")fail("invalid lock entry");
 splitSpec(c+"@"+v);safePath(entry);if(!valid(integrity,"sha256:[0-9a-f]{64}"))fail("invalid lock SHA256");
 deps.push_back({a,c,v,integrity});}return deps;
}
void add(const std::string& spec){
 auto [coord,version]=splitSpec(spec);auto pkg=fetch(coord,version);
 auto manifest=findProject();auto source=read(manifest);std::string a=alias(coord);
 if(manifest.extension()==".nqon"){
  auto pos=source.find("packages");if(pos==std::string::npos){source+="\npackages {\n  "+a+" = "+escaped(coord+"@"+pkg.version)+"\n}\n";}
  else {auto end=source.find('}',pos);if(end==std::string::npos)fail("invalid packages block");source.insert(end,"  "+a+" = "+escaped(coord+"@"+pkg.version)+"\n");}
 }else{
  auto pos=source.find("dependencies");if(pos==std::string::npos)source+="\ndependencies {\n  "+a+": "+escaped(coord+"@"+pkg.version)+"\n}\n";
  else{auto end=source.find('}',pos);if(end==std::string::npos)fail("invalid dependencies");source.insert(end,"  "+a+": "+escaped(coord+"@"+pkg.version)+"\n");}
 }
 write(manifest,source);install(manifest);std::cout<<"added "<<coord<<"@"<<pkg.version<<"\n";
}
void resolve(const fs::path& source){
 auto manifest=findProject(source);auto deps=dependencies(read(manifest),manifest.filename().string());
 const std::regex rx(R"re(\bimport\s+package\s+"([A-Za-z0-9_-]+/[A-Za-z0-9_-]+)")re");
 auto input=read(source);
 for(auto i=std::sregex_iterator(input.begin(),input.end(),rx);i!=std::sregex_iterator();++i){
  auto coordinate=(*i)[1].str();auto dep=std::find_if(deps.begin(),deps.end(),[&](const Dep& d){return d.coordinate==coordinate;});
  if(dep==deps.end())fail("undeclared package "+coordinate+"; run noqeri pkg add "+coordinate);
  auto pkg=fetch(coordinate,dep->version);std::cout<<coordinate<<"@"<<pkg.version<<": "<<(pkg.path/pkg.entry).string()<<" ("<<pkg.integrity<<")\n";
 }
}
void vendor(){
 auto root=findProject().parent_path();auto list=lockDeps(root/"noqeri.lock");
 auto dest=root/".noqeri/vendor-home/cache/registry-v1";fs::remove_all(root/".noqeri/vendor-home");
 for(auto& dep:list){auto pkg=fetch(dep.coordinate,dep.version);if(pkg.integrity!=dep.integrity)fail("lock integrity mismatch: "+dep.coordinate);auto target=dest/dep.coordinate/dep.version;fs::create_directories(target.parent_path());
 fs::copy(pkg.path,target,fs::copy_options::recursive);std::cout<<"vendored "<<dep.coordinate<<"@"<<dep.version<<"\n";}
}
int audit(){
 auto root=findProject().parent_path();auto list=lockDeps(root/"noqeri.lock");
 auto advisory=env("NOQERI_ADVISORY_FILE");if(advisory.empty())advisory=(root/".noqeri/advisories/index.json").string();
 std::string data;
 if(fs::is_regular_file(advisory))data=read(advisory);
 else if(!offline())data=get(env("NOQERI_ADVISORY_URL","https://raw.githubusercontent.com/EMN90909/noqeri-registry/main/advisories/index.json"));
 else{std::cout<<"{\"status\":\"unknown\",\"error\":\"offline advisory index unavailable\"}\n";return 2;}
 auto doc=Parser(data).parse();if(doc.at("schema").str()!="1")fail("unsupported advisory schema");
 bool vulnerable=false;
 for(auto& dep:list)for(auto& item:doc.at("advisories").array){
  if(item.at("package").str()!=dep.coordinate)continue;
  auto found=item.object.find("affected_versions");if(found==item.object.end()){std::cout<<"{\"status\":\"unknown\",\"error\":\"unsupported advisory range; cannot assert clean\"}\n";return 2;}
  for(auto& v:found->second.array)if(v.str()==dep.version){vulnerable=true;std::cout<<item.at("id").str()<<": "<<dep.coordinate<<"@"<<dep.version<<"\n";}
 }
 if(!vulnerable)std::cout<<"{\"status\":\"clean\",\"packages_checked\":"<<list.size()<<"}\n";
 return vulnerable?1:0;
}
}
int main(int argc,char** argv){
 try{
  if(argc<2||std::string(argv[1])=="help"||std::string(argv[1])=="--help"){
   std::cout<<"Noqeri native registry (Node.js-free)\n"
   <<"  noqeri pkg add namespace/name[@version]\n  noqeri pkg install\n  noqeri pkg resolve file.noqeri\n"
   <<"  noqeri pkg vendor [--offline]\n  noqeri pkg audit [--offline]\n";return 0;}
  std::string command=argv[1];if(command=="add"){if(argc!=3)fail("add expects one coordinate");add(argv[2]);}
  else if(command=="install"){if(argc!=2)fail("install accepts no arguments");install(findProject());}
  else if(command=="resolve"){if(argc!=3)fail("resolve expects source path");resolve(argv[2]);}
  else if(command=="vendor"||command=="audit"){
   if(argc>3||(argc==3&&std::string(argv[2])!="--offline"))fail("unsupported flag");
   if(argc==3){
#ifdef _WIN32
    _putenv_s("NOQERI_OFFLINE","1");
#else
    setenv("NOQERI_OFFLINE","1",1);
#endif
   }
   if(command=="vendor")vendor();else return audit();
  }else fail("unknown package command: "+command);
  return 0;
 }catch(const std::exception& e){std::cerr<<"noqeri pkg: "<<e.what()<<"\n";return 2;}
}
