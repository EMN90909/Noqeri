#include "noe.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace noe {
namespace {

enum class ManifestTokenKind { Identifier, String, LBrace, RBrace, Colon, Eof };
struct ManifestToken { ManifestTokenKind kind=ManifestTokenKind::Eof; std::string text; std::size_t offset=0; };

class ManifestLexer {
public:
    explicit ManifestLexer(std::string input):input_(std::move(input)){}
    std::vector<ManifestToken> lex(Diagnostics& diagnostics) {
        std::vector<ManifestToken> out;
        for(;;) {
            skipSpaceAndComments(diagnostics);
            if(pos_>=input_.size()) { out.push_back({ManifestTokenKind::Eof,"",pos_}); break; }
            const auto start=pos_;
            const char c=input_[pos_++];
            if(std::isalpha(static_cast<unsigned char>(c))||c=='_') {
                while(pos_<input_.size()) {
                    const char n=input_[pos_];
                    if(!std::isalnum(static_cast<unsigned char>(n))&&n!='_'&&n!='-'&&n!='.')break;
                    ++pos_;
                }
                out.push_back({ManifestTokenKind::Identifier,input_.substr(start,pos_-start),start});
                continue;
            }
            if(c=='"') {
                std::string value;
                bool closed=false;
                while(pos_<input_.size()) {
                    char n=input_[pos_++];
                    if(n=='"'){closed=true;break;}
                    if(n=='\\'&&pos_<input_.size()) {
                        const char e=input_[pos_++];
                        switch(e){case 'n':value+='\n';break;case 'r':value+='\r';break;case 't':value+='\t';break;case '\\':value+='\\';break;case '"':value+='"';break;default:value+=e;break;}
                    } else value+=n;
                }
                if(!closed)diagnostics.error("NQR-PKG6010",{},"unterminated string in project.noqeri");
                out.push_back({ManifestTokenKind::String,std::move(value),start});
                continue;
            }
            if(c=='{'){out.push_back({ManifestTokenKind::LBrace,"{",start});continue;}
            if(c=='}'){out.push_back({ManifestTokenKind::RBrace,"}",start});continue;}
            if(c==':'){out.push_back({ManifestTokenKind::Colon,":",start});continue;}
            diagnostics.error("NQR-PKG6011",{},std::string("unexpected manifest character '")+c+"'");
        }
        return out;
    }
private:
    void skipSpaceAndComments(Diagnostics& diagnostics) {
        for(;;) {
            while(pos_<input_.size()&&std::isspace(static_cast<unsigned char>(input_[pos_])))++pos_;
            if(pos_+1<input_.size()&&input_[pos_]=='/'&&input_[pos_+1]=='/') {
                pos_+=2;while(pos_<input_.size()&&input_[pos_]!='\n')++pos_;continue;
            }
            if(pos_+1<input_.size()&&input_[pos_]=='/'&&input_[pos_+1]=='*') {
                pos_+=2;bool closed=false;while(pos_+1<input_.size()){if(input_[pos_]=='*'&&input_[pos_+1]=='/'){pos_+=2;closed=true;break;}++pos_;}
                if(!closed)diagnostics.error("NQR-PKG6012",{},"unterminated block comment in project.noqeri");
                continue;
            }
            break;
        }
    }
    std::string input_;
    std::size_t pos_=0;
};

class ManifestParser {
public:
    ManifestParser(std::vector<ManifestToken> tokens,Diagnostics& diagnostics):tokens_(std::move(tokens)),diagnostics_(diagnostics){}
    std::optional<ProjectManifest> parse() {
        ProjectManifest manifest;
        bool projectSeen=false;
        while(!check(ManifestTokenKind::Eof)) {
            if(!check(ManifestTokenKind::Identifier)){fail("expected manifest block name");return std::nullopt;}
            const auto block=advance().text;
            if(!consume(ManifestTokenKind::LBrace,"expected '{' after manifest block"))return std::nullopt;
            if(block=="project"||block=="package") { parseProject(manifest); projectSeen=true; }
            else if(block=="dependencies"||block=="devDependencies") parseDependencies(manifest);
            else skipBlock();
            if(diagnostics_.hasErrors())return std::nullopt;
        }
        if(!projectSeen){diagnostics_.error("NQR-PKG6001",{},"project.noqeri must contain a project { ... } block");return std::nullopt;}
        return manifest;
    }
private:
    void parseProject(ProjectManifest& manifest) {
        while(!check(ManifestTokenKind::RBrace)&&!check(ManifestTokenKind::Eof)) {
            auto pair=parseStringPair();if(!pair)return;
            const auto&[key,value]=*pair;
            if(key=="name")manifest.name=value;
            else if(key=="version")manifest.version=value;
            else if(key=="edition")manifest.edition=value;
            else if(key=="entry")manifest.entry=value;
            else if(key=="profile")manifest.profile=value;
            else if(key=="target")manifest.target=value;
            else if(key=="registry")manifest.registry=value;
        }
        consume(ManifestTokenKind::RBrace,"expected '}' after project block");
    }
    void parseDependencies(ProjectManifest& manifest) {
        while(!check(ManifestTokenKind::RBrace)&&!check(ManifestTokenKind::Eof)) {
            if(!check(ManifestTokenKind::Identifier)){fail("expected dependency alias");return;}
            const auto alias=advance().text;
            if(!consume(ManifestTokenKind::Colon,"expected ':' after dependency alias"))return;
            DependencySpec spec;spec.package=alias;
            if(check(ManifestTokenKind::String)) {
                const auto value=advance().text;
                auto at=value.rfind('@');
                if(at!=std::string::npos&&at>0){spec.package=value.substr(0,at);spec.version=value.substr(at+1);}else spec.version=value;
            } else if(check(ManifestTokenKind::LBrace)) {
                advance();
                while(!check(ManifestTokenKind::RBrace)&&!check(ManifestTokenKind::Eof)) {
                    auto pair=parseStringPair();if(!pair)return;
                    const auto&[key,value]=*pair;
                    if(key=="package")spec.package=value;
                    else if(key=="version")spec.version=value;
                    else if(key=="checksum")spec.checksum=value;
                    else if(key=="path"){spec.source=DependencySource::Path;spec.location=value;}
                    else if(key=="git"){spec.source=DependencySource::Git;spec.location=value;}
                    else if(key=="registry"){spec.source=DependencySource::Registry;spec.location=value;}
                }
                if(!consume(ManifestTokenKind::RBrace,"expected '}' after dependency specification"))return;
            } else { fail("dependency must be a string or object"); return; }
            if(spec.version.empty()&&spec.source!=DependencySource::Path){diagnostics_.error("NQR-PKG6014",{},"dependency '"+alias+"' requires a version");return;}
            manifest.dependencySpecs[alias]=spec;
            manifest.dependencies[alias]=spec.version;
        }
        consume(ManifestTokenKind::RBrace,"expected '}' after dependencies block");
    }
    std::optional<std::pair<std::string,std::string>> parseStringPair() {
        if(!check(ManifestTokenKind::Identifier)){fail("expected manifest field name");return std::nullopt;}
        const auto key=advance().text;
        if(!consume(ManifestTokenKind::Colon,"expected ':' after manifest field"))return std::nullopt;
        if(!check(ManifestTokenKind::String)){fail("manifest field values must be strings");return std::nullopt;}
        return std::make_pair(key,advance().text);
    }
    void skipBlock(){int depth=1;while(depth>0&&!check(ManifestTokenKind::Eof)){auto k=advance().kind;if(k==ManifestTokenKind::LBrace)++depth;else if(k==ManifestTokenKind::RBrace)--depth;}}
    bool check(ManifestTokenKind kind)const{return tokens_[current_].kind==kind;}
    ManifestToken advance(){return tokens_[current_++];}
    bool consume(ManifestTokenKind kind,const std::string&message){if(check(kind)){advance();return true;}fail(message);return false;}
    void fail(const std::string&message){diagnostics_.error("NQR-PKG6013",{},message);}
    std::vector<ManifestToken>tokens_;Diagnostics&diagnostics_;std::size_t current_=0;
};

std::string trimNqon(std::string value){
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return{};
    const auto last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}
std::string stripNqonComment(const std::string&line){
    bool inString=false,escape=false;
    for(std::size_t i=0;i+1<line.size();++i){
        const char c=line[i];
        if(inString){if(escape){escape=false;continue;}if(c=='\\'){escape=true;continue;}if(c=='"')inString=false;continue;}
        if(c=='"'){inString=true;continue;}
        if(c=='/'&&line[i+1]=='/')return line.substr(0,i);
    }
    return line;
}
std::optional<std::string> nqonScalar(std::string value,Diagnostics&diagnostics){
    value=trimNqon(value);
    if(value.empty()){diagnostics.error("NQR-PKG6030",{},"NQON manifest value is empty");return std::nullopt;}
    if(value.front()!='"')return value;
    if(value.size()<2||value.back()!='"'){diagnostics.error("NQR-PKG6031",{},"unterminated string in project.nqon");return std::nullopt;}
    std::string out;bool escape=false;
    for(std::size_t i=1;i+1<value.size();++i){const char c=value[i];if(escape){switch(c){case'n':out+='\n';break;case'r':out+='\r';break;case't':out+='\t';break;case'\\':out+='\\';break;case'"':out+='"';break;default:out+=c;break;}escape=false;}else if(c=='\\')escape=true;else out+=c;}
    if(escape){diagnostics.error("NQR-PKG6032",{},"unterminated escape in project.nqon");return std::nullopt;}
    return out;
}
void applyNqonProjectField(ProjectManifest&manifest,const std::string&key,const std::string&value){
    if(key=="name")manifest.name=value;
    else if(key=="version")manifest.version=value;
    else if(key=="edition")manifest.edition=value;
    else if(key=="entry")manifest.entry=value;
    else if(key=="profile")manifest.profile=value;
    else if(key=="target")manifest.target=value;
    else if(key=="registry")manifest.registry=value;
}
std::optional<ProjectManifest> parseNqonProjectManifest(const std::string&source,Diagnostics&diagnostics){
    ProjectManifest manifest;std::istringstream input(source);std::string raw;bool packages=false;std::size_t line=0;
    while(std::getline(input,raw)){++line;auto text=trimNqon(stripNqonComment(raw));if(text.empty())continue;
        if(text=="packages {"||text=="dependencies {"){if(packages){diagnostics.error("NQR-PKG6033",{},"nested package blocks are not allowed in project.nqon");return std::nullopt;}packages=true;continue;}
        if(text=="}"){if(!packages){diagnostics.error("NQR-PKG6034",{},"unexpected '}' in project.nqon");return std::nullopt;}packages=false;continue;}
        const auto equals=text.find('=');if(equals==std::string::npos){diagnostics.error("NQR-PKG6035",{},"expected key = value in project.nqon at line "+std::to_string(line));return std::nullopt;}
        const auto key=trimNqon(text.substr(0,equals));auto value=nqonScalar(text.substr(equals+1),diagnostics);if(!value)return std::nullopt;
        if(packages){DependencySpec spec;const auto at=value->rfind('@');if(at==std::string::npos||at==0||at+1>=value->size()){diagnostics.error("NQR-PKG6036",{},"package '"+key+"' must use namespace/name@version in project.nqon");return std::nullopt;}spec.package=value->substr(0,at);spec.version=value->substr(at+1);manifest.dependencySpecs[key]=spec;manifest.dependencies[key]=spec.version;}
        else applyNqonProjectField(manifest,key,*value);
    }
    if(packages){diagnostics.error("NQR-PKG6037",{},"unterminated packages block in project.nqon");return std::nullopt;}
    if(manifest.name.empty()){diagnostics.error("NQR-PKG6038",{},"project.nqon requires a name");return std::nullopt;}
    return manifest;
}

class Sha256 {
public:
    Sha256(){reset();}
    void update(const unsigned char*data,std::size_t len){for(std::size_t i=0;i<len;++i){buffer_[bufferLength_++]=data[i];if(bufferLength_==64){transform();bitLength_+=512;bufferLength_=0;}}}
    void update(std::string_view value){update(reinterpret_cast<const unsigned char*>(value.data()),value.size());}
    std::string finish(){std::size_t i=bufferLength_;buffer_[i++]=0x80;if(i>56){while(i<64)buffer_[i++]=0;transform();i=0;}while(i<56)buffer_[i++]=0;bitLength_+=static_cast<std::uint64_t>(bufferLength_)*8;for(int j=7;j>=0;--j)buffer_[i++]=static_cast<unsigned char>((bitLength_>>(j*8))&0xff);transform();std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:state_)out<<std::setw(8)<<word;reset();return out.str();}
private:
    static std::uint32_t rotr(std::uint32_t x,std::uint32_t n){return(x>>n)|(x<<(32-n));}
    void reset(){state_={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};bufferLength_=0;bitLength_=0;}
    void transform(){static constexpr std::array<std::uint32_t,64>k={0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};std::uint32_t w[64]{};for(std::size_t i=0;i<16;++i)w[i]=(static_cast<std::uint32_t>(buffer_[i*4])<<24)|(static_cast<std::uint32_t>(buffer_[i*4+1])<<16)|(static_cast<std::uint32_t>(buffer_[i*4+2])<<8)|buffer_[i*4+3];for(std::size_t i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];for(std::size_t i=0;i<64;++i){auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=h+s1+ch+k[i]+w[i];auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;}
    std::array<std::uint32_t,8>state_{};std::array<unsigned char,64>buffer_{};std::size_t bufferLength_=0;std::uint64_t bitLength_=0;
};

std::optional<std::string> hashPath(const std::filesystem::path&root,Diagnostics&diagnostics){std::error_code ec;if(!std::filesystem::exists(root)){diagnostics.error("NQR-PKG6020",{},"path dependency not found: "+root.string());return std::nullopt;}std::vector<std::filesystem::path>files;if(std::filesystem::is_regular_file(root))files.push_back(root);else for(const auto&entry:std::filesystem::recursive_directory_iterator(root,ec)){if(ec){diagnostics.error("NQR-PKG6021",{},"cannot inspect path dependency: "+ec.message());return std::nullopt;}if(entry.is_regular_file())files.push_back(entry.path());}std::sort(files.begin(),files.end(),[](const auto&a,const auto&b){return a.generic_string()<b.generic_string();});Sha256 sha;for(const auto&file:files){auto relative=std::filesystem::is_directory(root)?std::filesystem::relative(file,root,ec).generic_string():file.filename().generic_string();sha.update(relative);const unsigned char zero=0;sha.update(&zero,1);std::ifstream in(file,std::ios::binary);std::array<char,8192>buf{};while(in){in.read(buf.data(),static_cast<std::streamsize>(buf.size()));auto n=in.gcount();if(n>0)sha.update(reinterpret_cast<const unsigned char*>(buf.data()),static_cast<std::size_t>(n));}sha.update(&zero,1);}return"sha256:"+sha.finish();}

std::string sourceName(DependencySource source){switch(source){case DependencySource::Registry:return"registry";case DependencySource::Git:return"git";case DependencySource::Path:return"path";}return"registry";}
}

std::optional<ProjectManifest> PackageManager::loadManifest(const std::filesystem::path&path,Diagnostics&diagnostics)const{std::ifstream in(path,std::ios::binary);if(!in){diagnostics.error("NQR-PKG6000",{},"project manifest not found: "+path.string());return std::nullopt;}std::ostringstream source;source<<in.rdbuf();if(path.extension()==".nqon")return parseNqonProjectManifest(source.str(),diagnostics);ManifestLexer lexer(source.str());auto tokens=lexer.lex(diagnostics);if(diagnostics.hasErrors())return std::nullopt;return ManifestParser(std::move(tokens),diagnostics).parse();}

bool PackageManager::writeLock(const ProjectManifest&manifest,const std::filesystem::path&path,Diagnostics&diagnostics)const{std::map<std::string,DependencySpec>sorted;if(!manifest.dependencySpecs.empty())sorted.insert(manifest.dependencySpecs.begin(),manifest.dependencySpecs.end());else for(const auto&[alias,version]:manifest.dependencies){DependencySpec spec;spec.package=alias;spec.version=version;sorted[alias]=spec;}std::ostringstream text;text<<"noqeri-lock 2\n";text<<"project "<<manifest.name<<' '<<manifest.version<<" edition "<<manifest.edition<<" target "<<manifest.target<<"\n";text<<"registry "<<manifest.registry<<"\n";for(auto&[alias,spec]:sorted){std::string checksum=spec.checksum;if(spec.source==DependencySource::Path){std::filesystem::path location=spec.location;if(location.is_relative())location=path.parent_path()/location;auto calculated=hashPath(location,diagnostics);if(!calculated)return false;checksum=*calculated;}else if(checksum.empty()){diagnostics.error("NQR-PKG6022",{},"dependency '"+alias+"' has no immutable content checksum","resolve the package through the registry and record its sha256 checksum before locking");return false;}if(checksum.rfind("sha256:",0)!=0){diagnostics.error("NQR-PKG6023",{},"dependency '"+alias+"' checksum must use sha256:<hex>");return false;}text<<"dependency "<<alias<<' '<<spec.package<<' '<<(spec.version.empty()?"path":spec.version)<<" source "<<sourceName(spec.source);if(!spec.location.empty())text<<' '<<spec.location;text<<" checksum "<<checksum<<"\n";}std::ofstream out(path,std::ios::trunc|std::ios::binary);if(!out){diagnostics.error("NQR-PKG6002",{},"cannot write lock file: "+path.string());return false;}out<<text.str();return static_cast<bool>(out);}

bool PackageManager::createProject(const std::filesystem::path&directory,const std::string&name,Diagnostics&diagnostics)const{std::error_code ec;if(std::filesystem::exists(directory)&&!std::filesystem::is_empty(directory,ec)){diagnostics.error("NQR-PKG6003",{},"project directory is not empty: "+directory.string());return false;}std::filesystem::create_directories(directory/"src",ec);std::filesystem::create_directories(directory/"tests",ec);if(ec){diagnostics.error("NQR-PKG6004",{},"cannot create project directories: "+ec.message());return false;}{std::ofstream project(directory/"project.nqon");if(!project){diagnostics.error("NQR-PKG6005",{},"cannot create project.nqon");return false;}project<<"name = "<<name<<"\nversion = \"0.1.0\"\n";}{std::ofstream main(directory/"src"/"main.noqeri");if(!main){diagnostics.error("NQR-PKG6006",{},"cannot create src/main.noqeri");return false;}main<<"print(\"Hello from Noqeri\")\n";}ProjectManifest manifest;manifest.name=name;manifest.version="0.1.0";manifest.edition=NOQERI_EDITION;manifest.target=NOQERI_DEFAULT_TARGET;return writeLock(manifest,directory/"noqeri.lock",diagnostics);}

} // namespace noe
