#include "noe.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace noe {
namespace {

enum class Tok { Id, String, Number, LBrace, RBrace, Colon, Comma, Semi, Equal, Eof };
struct Token { Tok kind=Tok::Eof; std::string text; std::size_t line=1,column=1; };

class Lexer {
public:
    explicit Lexer(std::string input):input_(std::move(input)){}
    std::vector<Token> run(Diagnostics& d){
        std::vector<Token> out;
        while(true){skip();if(pos_>=input_.size()){out.push_back({Tok::Eof,"",line_,column_});break;}
            const auto line=line_,col=column_;char c=take();
            if(std::isalpha(static_cast<unsigned char>(c))||c=='_'){
                std::string s(1,c);while(pos_<input_.size()){char n=input_[pos_];if(!std::isalnum(static_cast<unsigned char>(n))&&n!='_'&&n!='-')break;s+=take();}out.push_back({Tok::Id,s,line,col});continue;}
            if(std::isdigit(static_cast<unsigned char>(c))||(c=='-'&&pos_<input_.size()&&std::isdigit(static_cast<unsigned char>(input_[pos_])))){
                std::string s(1,c);bool dot=false;while(pos_<input_.size()){char n=input_[pos_];if(n=='.'&&!dot){dot=true;s+=take();continue;}if(!std::isdigit(static_cast<unsigned char>(n)))break;s+=take();}out.push_back({Tok::Number,s,line,col});continue;}
            if(c=='\"'){
                std::string s;bool closed=false;while(pos_<input_.size()){char n=take();if(n=='\"'){closed=true;break;}if(n=='\\'&&pos_<input_.size()){char e=take();if(e=='n')s+='\n';else if(e=='r')s+='\r';else if(e=='t')s+='\t';else s+=e;}else s+=n;}if(!closed)d.error("NQR-D8001",{0,line,col},"unterminated .nqd string");out.push_back({Tok::String,s,line,col});continue;}
            Tok kind;bool ok=true;switch(c){case'{':kind=Tok::LBrace;break;case'}':kind=Tok::RBrace;break;case':':kind=Tok::Colon;break;case',':kind=Tok::Comma;break;case';':kind=Tok::Semi;break;case'=':kind=Tok::Equal;break;default:ok=false;break;}if(ok)out.push_back({kind,std::string(1,c),line,col});else d.error("NQR-D8002",{0,line,col},std::string("unexpected .nqd character '")+c+"'");
        }return out;
    }
private:
    char take(){char c=input_[pos_++];if(c=='\n'){++line_;column_=1;}else ++column_;return c;}
    void skip(){for(;;){while(pos_<input_.size()&&std::isspace(static_cast<unsigned char>(input_[pos_])))take();if(pos_+1<input_.size()&&input_[pos_]=='/'&&input_[pos_+1]=='/'){while(pos_<input_.size()&&take()!='\n'){}continue;}if(pos_<input_.size()&&input_[pos_]=='#'){while(pos_<input_.size()&&take()!='\n'){}continue;}break;}}
    std::string input_;std::size_t pos_=0,line_=1,column_=1;
};

enum class ValueType { Int, Real, Bool, Text };
using Value = std::variant<std::int64_t,double,bool,std::string>;
struct Column { std::string name;ValueType type=ValueType::Text;bool required=false,unique=false,key=false; };
using Row = std::map<std::string,Value>;
struct Table { std::string name;std::vector<Column> columns;std::vector<Row> rows; };
struct State { std::map<std::string,Table> tables; };

std::string lower(std::string s){for(char&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string typeName(ValueType t){switch(t){case ValueType::Int:return"int";case ValueType::Real:return"real";case ValueType::Bool:return"bool";case ValueType::Text:return"text";}return"text";}
std::optional<ValueType> parseType(const std::string&s){auto x=lower(s);if(x=="int"||x=="integer")return ValueType::Int;if(x=="real"||x=="float")return ValueType::Real;if(x=="bool")return ValueType::Bool;if(x=="text"||x=="string")return ValueType::Text;return std::nullopt;}
std::string valueText(const Value&v){if(auto p=std::get_if<std::int64_t>(&v))return std::to_string(*p);if(auto p=std::get_if<double>(&v)){std::ostringstream o;o<<std::setprecision(17)<<*p;return o.str();}if(auto p=std::get_if<bool>(&v))return*p?"true":"false";return std::get<std::string>(v);}
bool valueEqual(const Value&a,const Value&b){if(a.index()==b.index())return a==b;if(auto x=std::get_if<std::int64_t>(&a))if(auto y=std::get_if<double>(&b))return static_cast<double>(*x)==*y;if(auto x=std::get_if<double>(&a))if(auto y=std::get_if<std::int64_t>(&b))return*x==static_cast<double>(*y);return false;}
std::string hex(const std::string&s){static const char*d="0123456789abcdef";std::string out;out.reserve(s.size()*2);for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
std::optional<std::string> unhex(const std::string&s){if(s.size()%2)return std::nullopt;auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return 10+c-'a';if(c>='A'&&c<='F')return 10+c-'A';return-1;};std::string out;out.reserve(s.size()/2);for(std::size_t i=0;i<s.size();i+=2){int a=nib(s[i]),b=nib(s[i+1]);if(a<0||b<0)return std::nullopt;out+=static_cast<char>((a<<4)|b);}return out;}
std::string encodeValue(const Value&v){return std::to_string(v.index())+":"+hex(valueText(v));}
std::optional<Value> decodeValue(const std::string&s){auto p=s.find(':');if(p==std::string::npos)return std::nullopt;int kind=0;try{kind=std::stoi(s.substr(0,p));}catch(...){return std::nullopt;}auto raw=unhex(s.substr(p+1));if(!raw)return std::nullopt;try{if(kind==0)return Value(static_cast<std::int64_t>(std::stoll(*raw)));if(kind==1)return Value(std::stod(*raw));if(kind==2)return Value(*raw=="true");if(kind==3)return Value(*raw);}catch(...){return std::nullopt;}return std::nullopt;}

bool saveState(const std::filesystem::path&path,const State&state,Diagnostics&d){
    std::error_code ec;auto parent=path.parent_path();if(!parent.empty())std::filesystem::create_directories(parent,ec);if(ec){d.error("NQR-D8020",{},"cannot create database directory: "+ec.message());return false;}
    auto temp=path;temp+=".tmp";std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out){d.error("NQR-D8021",{},"cannot write NoqeriDB file: "+temp.string());return false;}out<<"NQDB1\n";
    for(const auto&[name,t]:state.tables){out<<"TABLE "<<hex(name)<<' '<<t.columns.size()<<' '<<t.rows.size()<<"\n";for(const auto&c:t.columns)out<<"COL "<<hex(c.name)<<' '<<typeName(c.type)<<' '<<(c.required?1:0)<<' '<<(c.unique?1:0)<<' '<<(c.key?1:0)<<"\n";for(const auto&r:t.rows){out<<"ROW "<<r.size()<<"\n";for(const auto&[k,v]:r)out<<"VAL "<<hex(k)<<' '<<encodeValue(v)<<"\n";}out<<"ENDTABLE\n";}out.flush();if(!out){d.error("NQR-D8022",{},"failed while writing NoqeriDB file");return false;}out.close();
    // Never delete the last good database before the replacement is committed.
    // Platforms without atomic replace-over-existing must fail closed rather than lose data.
    std::filesystem::rename(temp,path,ec);if(ec){d.error("NQR-D8023",{},"cannot commit NoqeriDB transaction: "+ec.message());return false;}return true;
}

bool loadState(const std::filesystem::path&path,State&state,Diagnostics&d){
    if(!std::filesystem::exists(path))return true;std::ifstream in(path,std::ios::binary);std::string line;if(!std::getline(in,line)||line!="NQDB1"){d.error("NQR-D8030",{},"invalid NoqeriDB header");return false;}
    Table* current=nullptr;std::size_t expectedCols=0,expectedRows=0,seenRows=0;Row row;std::size_t expectedVals=0;
    while(std::getline(in,line)){std::istringstream s(line);std::string op;s>>op;if(op=="TABLE"){std::string n;s>>n>>expectedCols>>expectedRows;auto decoded=unhex(n);if(!decoded){d.error("NQR-D8031",{},"corrupt table name");return false;}auto& t=state.tables[*decoded];t.name=*decoded;t.columns.clear();t.rows.clear();current=&t;seenRows=0;}else if(op=="COL"&&current){std::string n,ty;int req=0,uni=0,key=0;s>>n>>ty>>req>>uni>>key;auto decoded=unhex(n);auto type=parseType(ty);if(!decoded||!type){d.error("NQR-D8032",{},"corrupt column metadata");return false;}current->columns.push_back({*decoded,*type,req!=0,uni!=0,key!=0});}else if(op=="ROW"&&current){s>>expectedVals;row.clear();while(expectedVals--){if(!std::getline(in,line)){d.error("NQR-D8033",{},"truncated row");return false;}std::istringstream vline(line);std::string tag,k,v;vline>>tag>>k>>v;if(tag!="VAL"){d.error("NQR-D8034",{},"invalid row value record");return false;}auto key=unhex(k);auto value=decodeValue(v);if(!key||!value){d.error("NQR-D8035",{},"corrupt row value");return false;}row[*key]=*value;}current->rows.push_back(row);++seenRows;}else if(op=="ENDTABLE"&&current){if(current->columns.size()!=expectedCols||seenRows!=expectedRows){d.error("NQR-D8036",{},"NoqeriDB table count mismatch");return false;}current=nullptr;}else if(!op.empty()){d.error("NQR-D8037",{},"unknown NoqeriDB record: "+op);return false;}}
    if(current){d.error("NQR-D8038",{},"truncated NoqeriDB table");return false;}return true;
}

class Parser {
public:
    Parser(std::vector<Token> t,std::filesystem::path script,std::filesystem::path overridePath,std::ostream&out,Diagnostics&d):t_(std::move(t)),script_(std::move(script)),dbPath_(std::move(overridePath)),out_(out),d_(d){}
    bool run(){
        if(dbPath_.empty()){dbPath_=script_;dbPath_.replace_extension(".nqdb");}
        if(!loadState(dbPath_,state_,d_))return false;
        while(!at(Tok::Eof)){if(!statement())return false;match(Tok::Semi);}
        return !d_.hasErrors()&&(!dirty_||saveState(dbPath_,state_,d_));
    }
private:
    const Token&peek()const{return t_[i_];}const Token&take(){return t_[i_++];}bool at(Tok k)const{return peek().kind==k;}bool match(Tok k){if(at(k)){++i_;return true;}return false;}
    bool error(const std::string&m){auto&p=peek();d_.error("NQR-D8000",{0,p.line,p.column},m);return false;}
    std::optional<std::string> id(){if(!at(Tok::Id)){error("expected identifier");return std::nullopt;}return take().text;}
    bool keyword(const char*k){return at(Tok::Id)&&lower(peek().text)==k;}
    bool consumeKeyword(const char*k){if(!keyword(k))return error(std::string("expected '")+k+"'");take();return true;}
    std::optional<Value> value(){if(at(Tok::String))return Value(take().text);if(at(Tok::Number)){auto s=take().text;try{if(s.find('.')!=std::string::npos)return Value(std::stod(s));return Value(static_cast<std::int64_t>(std::stoll(s)));}catch(...){error("invalid numeric value");return std::nullopt;}}if(keyword("true")){take();return Value(true);}if(keyword("false")){take();return Value(false);}error("expected string, number, true or false");return std::nullopt;}
    std::optional<Row> object(){if(!match(Tok::LBrace)){error("expected '{'");return std::nullopt;}Row r;if(!at(Tok::RBrace))for(;;){auto k=id();if(!k||!match(Tok::Colon)){error("expected ':' after field name");return std::nullopt;}auto v=value();if(!v)return std::nullopt;r[*k]=*v;if(match(Tok::Comma))continue;break;}if(!match(Tok::RBrace)){error("expected '}'");return std::nullopt;}return r;}
    const Column* column(const Table&t,const std::string&name)const{for(const auto&c:t.columns)if(c.name==name)return&c;return nullptr;}
    bool compatible(ValueType t,const Value&v)const{return(t==ValueType::Int&&std::holds_alternative<std::int64_t>(v))||(t==ValueType::Real&&(std::holds_alternative<double>(v)||std::holds_alternative<std::int64_t>(v)))||(t==ValueType::Bool&&std::holds_alternative<bool>(v))||(t==ValueType::Text&&std::holds_alternative<std::string>(v));}
    bool validate(const Table&t,const Row&r,std::optional<std::size_t>skip={}){for(const auto&[k,v]:r){auto*c=column(t,k);if(!c)return error("unknown column '"+k+"' in table '"+t.name+"'");if(!compatible(c->type,v))return error("value for '"+k+"' does not match "+typeName(c->type));}for(const auto&c:t.columns){if(c.required&&!r.count(c.name))return error("required column '"+c.name+"' is missing");if((c.unique||c.key)&&r.count(c.name)){for(std::size_t n=0;n<t.rows.size();++n){if(skip&&n==*skip)continue;auto it=t.rows[n].find(c.name);if(it!=t.rows[n].end()&&valueEqual(it->second,r.at(c.name)))return error("unique/key constraint failed for '"+c.name+"'");}}}return true;}
    bool matches(const Row&r,const std::optional<std::pair<std::string,Value>>&w)const{if(!w)return true;auto it=r.find(w->first);return it!=r.end()&&valueEqual(it->second,w->second);}
    std::optional<std::pair<std::string,Value>> where(){if(!keyword("where"))return std::optional<std::pair<std::string,Value>>{};take();auto k=id();if(!k||!match(Tok::Equal)){error("expected '=' in where clause");return std::nullopt;}auto v=value();if(!v)return std::nullopt;return std::make_pair(*k,*v);}
    bool statement(){
        if(keyword("database")){take();if(!at(Tok::String))return error("database expects a quoted .nqdb path");auto requested=std::filesystem::path(take().text);if(requested.extension()!=".nqdb")requested.replace_extension(".nqdb");if(requested.is_relative())requested=script_.parent_path()/requested;if(dbPath_!=requested&&std::filesystem::exists(dbPath_)&&state_.tables.size()){return error("database path must be declared before data statements");}dbPath_=requested;state_={};return loadState(dbPath_,state_,d_);}
        if(keyword("table")){take();auto name=id();if(!name)return false;if(!match(Tok::LBrace))return error("expected '{' after table name");Table t;t.name=*name;std::set<std::string>seen;while(!at(Tok::RBrace)&&!at(Tok::Eof)){auto cn=id();if(!cn||!match(Tok::Colon))return error("expected column type after ':'");auto ty=id();if(!ty)return false;auto parsed=parseType(*ty);if(!parsed)return error("unknown NoqeriDB type '"+*ty+"'");Column c{*cn,*parsed,false,false,false};while(at(Tok::Id)){auto flag=lower(peek().text);if(flag=="required"){c.required=true;take();}else if(flag=="unique"){c.unique=true;take();}else if(flag=="key"){c.key=true;c.unique=true;c.required=true;take();}else break;}if(!seen.insert(c.name).second)return error("duplicate column '"+c.name+"'");t.columns.push_back(c);if(match(Tok::Comma)||match(Tok::Semi))continue;}if(!match(Tok::RBrace))return error("expected '}' after table schema");auto it=state_.tables.find(*name);if(it==state_.tables.end()){state_.tables[*name]=std::move(t);dirty_=true;}else if(it->second.columns.size()!=t.columns.size())return error("existing table schema differs for '"+*name+"'");else {for(std::size_t c=0;c<t.columns.size();++c){const auto&a=it->second.columns[c],&b=t.columns[c];if(a.name!=b.name||a.type!=b.type||a.required!=b.required||a.unique!=b.unique||a.key!=b.key)return error("existing table schema differs for '"+*name+"'");}}return true;}
        if(keyword("insert")){take();auto n=id();if(!n)return false;auto it=state_.tables.find(*n);if(it==state_.tables.end())return error("unknown table '"+*n+"'");auto r=object();if(!r||!validate(it->second,*r))return false;it->second.rows.push_back(std::move(*r));dirty_=true;return true;}
        if(keyword("select")){take();auto n=id();if(!n)return false;auto it=state_.tables.find(*n);if(it==state_.tables.end())return error("unknown table '"+*n+"'");const bool hasWhere=keyword("where");auto w=where();if(hasWhere&&!w)return false;for(std::size_t c=0;c<it->second.columns.size();++c){if(c)out_<<'\t';out_<<it->second.columns[c].name;}out_<<'\n';for(const auto&r:it->second.rows)if(matches(r,w)){for(std::size_t c=0;c<it->second.columns.size();++c){if(c)out_<<'\t';auto v=r.find(it->second.columns[c].name);if(v!=r.end())out_<<valueText(v->second);}out_<<'\n';}return true;}
        if(keyword("delete")){take();auto n=id();if(!n)return false;auto it=state_.tables.find(*n);if(it==state_.tables.end())return error("unknown table '"+*n+"'");if(!keyword("where"))return error("delete requires an explicit where clause");auto w=where();if(!w)return false;auto&rows=it->second.rows;auto before=rows.size();rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Row&r){return matches(r,w);}),rows.end());dirty_=dirty_||rows.size()!=before;return true;}
        if(keyword("update")){take();auto n=id();if(!n)return false;auto it=state_.tables.find(*n);if(it==state_.tables.end())return error("unknown table '"+*n+"'");if(!consumeKeyword("set"))return false;auto patch=object();if(!patch)return false;if(!keyword("where"))return error("update requires an explicit where clause");auto w=where();if(!w)return false;for(std::size_t idx=0;idx<it->second.rows.size();++idx)if(matches(it->second.rows[idx],w)){Row candidate=it->second.rows[idx];for(const auto&[k,v]:*patch)candidate[k]=v;if(!validate(it->second,candidate,idx))return false;it->second.rows[idx]=std::move(candidate);dirty_=true;}return true;}
        return error("expected database, table, insert, select, update or delete statement");
    }
    std::vector<Token>t_;std::size_t i_=0;std::filesystem::path script_,dbPath_;std::ostream&out_;Diagnostics&d_;State state_;bool dirty_=false;
};
}

bool NoqeriDatabase::execute(const std::filesystem::path&script,const std::filesystem::path&databaseOverride,std::ostream&output,Diagnostics&diagnostics)const{
    std::ifstream in(script,std::ios::binary);if(!in){diagnostics.error("NQR-D8040",{},"cannot open .nqd script: "+script.string());return false;}std::ostringstream source;source<<in.rdbuf();Lexer lexer(source.str());auto tokens=lexer.run(diagnostics);if(diagnostics.hasErrors())return false;Parser parser(std::move(tokens),script,databaseOverride,output,diagnostics);return parser.run();
}

} // namespace noe
