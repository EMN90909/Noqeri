#include "noe.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace noe {
namespace {
Type simpleType(TypeKind kind){Type type;type.kind=kind;return type;}
std::size_t integerBits(TypeKind kind){switch(kind){case TypeKind::I8:case TypeKind::U8:return 8;case TypeKind::I16:case TypeKind::U16:return 16;case TypeKind::I32:case TypeKind::U32:return 32;case TypeKind::I64:case TypeKind::U64:case TypeKind::Isize:case TypeKind::Usize:case TypeKind::Int:return 64;default:return 0;}}
bool integerSigned(TypeKind kind){switch(kind){case TypeKind::I8:case TypeKind::I16:case TypeKind::I32:case TypeKind::I64:case TypeKind::Isize:case TypeKind::Int:return true;default:return false;}}
CompileResult compileProgram(Program program,Diagnostics diagnostics,const CompileOptions&options,std::shared_ptr<ModuleGraph>graph={}){CompileResult result;result.ast=std::move(program);result.diagnostics=std::move(diagnostics);result.modules=std::move(graph);if(result.diagnostics.hasErrors())return result;GenericEngine generics;if(!generics.monomorphize(result.ast,result.diagnostics))return result;TypeChecker checker(result.diagnostics);if(!checker.check(result.ast))return result;if(!SafetyAnnotator{}.check(result.ast,result.diagnostics,options.allowUnsafe))return result;BorrowChecker borrowChecker;if(!borrowChecker.check(result.ast,result.diagnostics))return result;Lowerer lowerer;result.nir=lowerer.lower(result.ast);NirAnalyzer analyzer;analyzer.analyze(result.ast,result.nir);Optimizer optimizer;optimizer.optimize(result.nir,options.optimization);analyzer.rebuildControlFlow(result.nir);return result;}
}
const char* tokenKindName(TokenKind k){switch(k){case TokenKind::Eof:return"eof";case TokenKind::Identifier:return"identifier";case TokenKind::Integer:return"integer";case TokenKind::Float:return"float";case TokenKind::String:return"string";case TokenKind::LineComment:return"line-comment";case TokenKind::BlockComment:return"block-comment";case TokenKind::Let:return"let";case TokenKind::Const:return"const";case TokenKind::Function:return"function";case TokenKind::If:return"if";case TokenKind::Else:return"else";case TokenKind::While:return"while";case TokenKind::Repeat:return"repeat";case TokenKind::Break:return"break";case TokenKind::Continue:return"continue";case TokenKind::Return:return"return";case TokenKind::True:return"true";case TokenKind::False:return"false";case TokenKind::Null:return"null";case TokenKind::Import:return"import";case TokenKind::Module:return"module";case TokenKind::Record:return"record";case TokenKind::Class:return"class";case TokenKind::Extern:return"extern";case TokenKind::Export:return"export";case TokenKind::Volatile:return"volatile";case TokenKind::As:return"as";case TokenKind::Throw:return"throw";case TokenKind::Try:return"try";case TokenKind::Unsafe:return"unsafe";case TokenKind::LParen:return"(";case TokenKind::RParen:return")";case TokenKind::LBrace:return"{";case TokenKind::RBrace:return"}";case TokenKind::LBracket:return"[";case TokenKind::RBracket:return"]";case TokenKind::Comma:return",";case TokenKind::Colon:return":";case TokenKind::Semicolon:return";";case TokenKind::Dot:return".";case TokenKind::At:return"@";case TokenKind::Plus:return"+";case TokenKind::Minus:return"-";case TokenKind::Star:return"*";case TokenKind::Slash:return"/";case TokenKind::Percent:return"%";case TokenKind::Ampersand:return"&";case TokenKind::Bang:return"!";case TokenKind::BangEqual:return"!=";case TokenKind::Equal:return"=";case TokenKind::EqualEqual:return"==";case TokenKind::Less:return"<";case TokenKind::LessEqual:return"<=";case TokenKind::Greater:return">";case TokenKind::GreaterEqual:return">=";case TokenKind::AndAnd:return"&&";case TokenKind::OrOr:return"||";case TokenKind::Question:return"?";case TokenKind::QuestionQuestion:return"??";}return"?";}
namespace {
std::string jsonQuoted(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch(c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(c) << std::dec;
                } else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}
}
void Diagnostics::error(std::string code,Span span,std::string message,std::string help) {
    if(code.rfind("NOE-",0)==0||code.rfind("RIC-",0)==0)code.replace(0,4,"NQR-");
    if(help.empty()) {
        if(code=="NQR-L1001")help="Add a closing double quote (\") to the string.";
        else if(code=="NQR-L1002")help="Close the comment with */.";
        else if(code=="NQR-L1003")help="Write at least one hexadecimal digit after 0x, for example 0xFF.";
        else if(code=="NQR-P2001")help="Write a value or expression here, such as 42, name, or add(1, 2).";
        else if(code=="NQR-P2000")help="Check the preceding punctuation or closing bracket.";
        else if(code=="NQR-T3000")help="Use a built-in type (int, float, bool, string) or declare a record type.";
        else if(code=="NQR-T3002")help="Declare this name with let or const before using it, or check the spelling.";
        else if(code=="NQR-T3011")help="Check the function name, or declare a function with that name.";
        else if(code=="NQR-T3004")help="Use a boolean expression, for example count > 0 or ready == true.";
        else if(code=="NQR-T3005")help="Return a value matching the function's declared return type.";
        else if(code=="NQR-T3012")help="Pass the number of arguments declared in the function signature.";
        else if(code=="NQR-T3013")help="Pass a value of the expected type or convert it explicitly with as.";
        else if(code=="NQR-T3014")help="Use let instead of const if this binding must change.";
        else if(code=="NQR-L1000")help="Remove this character or replace it with an operator or punctuation supported by Noqeri.";
        else if(code=="NQR-P2004")help="Assign to a declared variable or writable field, for example score = 10.";
        else if(code=="NQR-P2010")help="Only functions can be exported or external: export function name(...): Type { ... }.";
        else if(code=="NQR-P2011")help="Initialize the variable (let count = 0) or state its type (let count: int).";
        else if(code=="NQR-P2012")help="Initialize const immediately, for example const limit = 10.";
        else if(code=="NQR-P2021")help="Write function square(x: int) = x * x or annotate the return explicitly.";
        else if(code=="NQR-P2022")help="Use for item in values { ... } with an array or slice.";
        else if(code=="NQR-T3001")help="Change the initializer to match the declared type; use an explicit as cast for deliberate numeric conversions.";
        else if(code=="NQR-T3003")help="Rename one declaration or move it into a different scope.";
        else if(code=="NQR-T3006")help="Use numeric operands for arithmetic and bool for logical operators.";
        else if(code=="NQR-T3007")help="Replace the operand with int or float, or convert it explicitly.";
        else if(code=="NQR-T3010")help="Call a named function, such as print(value), instead of calling this expression.";
        else if(code=="NQR-T3021")help="Remove the duplicate declaration or make its parameter and return types match the forward declaration.";
        else if(code=="NQR-T3024")help="Check the field spelling and the record declaration.";
        else if(code=="NQR-T3025")help="Use an integer index, for example values[0].";
        else if(code=="NQR-T3026")help="Index an array or slice with [index], or use a record field with .name.";
        else if(code=="NQR-T3034")help="Start with a nonempty collection such as [1, 2], or give the empty array an explicit type.";
        else if(code=="NQR-T3035")help="Use compatible element types such as [1, 2, 3] rather than mixing numbers and strings.";
        else if(code=="NQR-T3057")help="Move break inside a repeat, while or for loop.";
        else if(code=="NQR-T3058")help="Move continue inside a repeat, while or for loop.";
        else if(code=="NQR-M6000"||code=="NQR-M6002")help="Use import \"./helper.noqeri\" for local files or import \"std/list\" for the standard library.";
        else if(code=="NQR-T3060"||code=="NQR-T3061")help="Add a return annotation, for example function answer(): int { return 42 }.";
        else help="Review the highlighted expression and the expected syntax/type; compare it with the matching example in Doc/LANGUAGE_REFERENCE.md.";
    }
    items_.push_back({std::move(code),std::move(message),span,std::move(help)});
}
void Diagnostics::print(const std::string& fallbackSourceName) const {
    constexpr std::size_t maxExcerpt = 160;
    for(const auto& d : items_) {
        const auto source = sourceName(d.span.source, fallbackSourceName);
        std::cerr << "error[" << d.code << "]: " << d.message
                  << "\n --> " << source << ':' << d.span.line << ':' << d.span.column << '\n';
        const auto found = sourceTexts_.find(d.span.source);
        // Source spans are byte-based. A bounded, single-line excerpt avoids
        // terminal flooding from very long source lines and invalid spans.
        if(found != sourceTexts_.end() && d.span.line > 0 && d.span.line <= 1000000) {
            const auto& text = found->second;
            std::size_t begin = 0;
            bool valid = true;
            for(std::size_t i = 1; i < d.span.line; ++i) {
                const auto pos = text.find('\n', begin);
                if(pos == std::string::npos) { valid = false; break; }
                begin = pos + 1;
            }
            if(valid) {
                auto end = text.find('\n', begin);
                if(end == std::string::npos) end = text.size();
                if(end > begin && text[end-1] == '\r') --end;
                const auto lineLength = end - begin;
                const auto rawOffset = d.span.column > 0 ? d.span.column - 1 : 0;
                const auto offset = std::min(rawOffset, lineLength);
                // Keep the error within the visible window even at a very
                // large column. Display one-byte-for-one-byte sanitization.
                const auto first = offset > maxExcerpt/2 ? offset - maxExcerpt/2 : 0;
                const auto start = std::min(first, lineLength);
                const auto shown = std::min(maxExcerpt, lineLength-start);
                std::string excerpt = text.substr(begin+start, shown);
                for(char& ch : excerpt) {
                    const auto byte = static_cast<unsigned char>(ch);
                    if(byte < 0x20 || byte == 0x7f) ch = ' ';
                }
                const auto prefix = start > 0 ? "..." : "";
                const auto suffix = start+shown < lineLength ? "..." : "";
                const auto caret = std::min(offset-start, shown);
                const auto spanBytes = d.span.end > d.span.start ? d.span.end-d.span.start : 1;
                const auto markCount = std::max<std::size_t>(
                    1, std::min<std::size_t>(spanBytes, std::max<std::size_t>(1,shown-caret)));
                const auto gutter = std::to_string(d.span.line);
                std::cerr << ' ' << gutter << " | " << prefix << excerpt << suffix << '\n'
                          << ' ' << std::string(gutter.size(), ' ') << " | "
                          << std::string((start>0?3:0)+caret, ' ')
                          << std::string(markCount, '^') << '\n';
            }
        }
        if(!d.help.empty()) std::cerr << " help: " << d.help << '\n';
    }
}
std::string Diagnostics::toJson(const std::string& fallbackSourceName) const {
    std::ostringstream out;
    out << '[';
    for(std::size_t i=0;i<items_.size();++i) {
        const auto& d=items_[i];
        if(i)out << ',';
        out << "{\"severity\":\"error\",\"code\":" << jsonQuoted(d.code)
            << ",\"message\":" << jsonQuoted(d.message)
            << ",\"file\":" << jsonQuoted(sourceName(d.span.source,fallbackSourceName))
            << ",\"span\":{\"start\":" << d.span.start << ",\"end\":" << d.span.end
            << ",\"line\":" << d.span.line << ",\"column\":" << d.span.column
            << "},\"help\":" << jsonQuoted(d.help) << '}';
    }
    out << ']';
    return out.str();
}
bool Type::isInteger()const{switch(kind){case TypeKind::I8:case TypeKind::I16:case TypeKind::I32:case TypeKind::I64:case TypeKind::U8:case TypeKind::U16:case TypeKind::U32:case TypeKind::U64:case TypeKind::Isize:case TypeKind::Usize:case TypeKind::Int:return true;default:return false;}}
std::size_t Type::size()const{switch(kind){case TypeKind::Bool:case TypeKind::I8:case TypeKind::U8:return 1;case TypeKind::I16:case TypeKind::U16:return 2;case TypeKind::I32:case TypeKind::U32:return 4;case TypeKind::I64:case TypeKind::U64:case TypeKind::Int:case TypeKind::Float:case TypeKind::Isize:case TypeKind::Usize:case TypeKind::Pointer:case TypeKind::String:case TypeKind::Null:case TypeKind::Generic:return 8;case TypeKind::Record:return recordSize;case TypeKind::Slice:return 16;case TypeKind::Array:{if(!element)return 0;const auto bytes=element->size();if(count!=0&&bytes>std::numeric_limits<std::size_t>::max()/count)throw std::overflow_error("fixed array size exceeds addressable range");return bytes*count;}default:return 0;}}
std::size_t Type::alignment()const{if(kind==TypeKind::Record)return recordAlignment?recordAlignment:1;if(kind==TypeKind::Slice)return 8;if(kind==TypeKind::Array&&element)return element->alignment();auto s=size();return s?s:1;}
std::string Type::name()const{switch(kind){case TypeKind::Unknown:return"unknown";case TypeKind::Void:return"void";case TypeKind::Null:return"null";case TypeKind::Bool:return"bool";case TypeKind::I8:return"i8";case TypeKind::I16:return"i16";case TypeKind::I32:return"i32";case TypeKind::I64:return"i64";case TypeKind::U8:return"u8";case TypeKind::U16:return"u16";case TypeKind::U32:return"u32";case TypeKind::U64:return"u64";case TypeKind::Isize:return"isize";case TypeKind::Usize:return"usize";case TypeKind::Int:return"int";case TypeKind::Float:return"float";case TypeKind::String:return"string";case TypeKind::Record:return recordName;case TypeKind::Generic:return genericName;case TypeKind::Pointer:return std::string("*")+(isVolatile?"volatile ":"")+(pointee?pointee->name():"unknown");case TypeKind::Slice:return std::string("[]")+(element?element->name():"unknown");case TypeKind::Array:return std::string("[")+(element?element->name():"unknown")+";"+std::to_string(count)+"]";}return"unknown";}
bool Type::operator==(const Type&o)const{if(kind!=o.kind||isVolatile!=o.isVolatile||recordName!=o.recordName||genericName!=o.genericName||count!=o.count)return false;if(kind==TypeKind::Pointer){if(!pointee||!o.pointee)return pointee==o.pointee;return*pointee==*o.pointee;}if(kind==TypeKind::Array||kind==TypeKind::Slice){if(!element||!o.element)return element==o.element;return*element==*o.element;}return true;}
Type typeFromName(const std::string&raw){std::string n=raw;if(n.rfind("*",0)==0){n.erase(0,1);bool vol=false;if(n.rfind("volatile ",0)==0){vol=true;n.erase(0,9);}Type p;p.kind=TypeKind::Pointer;p.isVolatile=vol;p.pointee=std::make_shared<Type>(typeFromName(n));return p;}if(n.rfind("[]",0)==0){Type s;s.kind=TypeKind::Slice;s.element=std::make_shared<Type>(typeFromName(n.substr(2)));return s;}if(n.size()>3&&n.front()=='['&&n.back()==']'){auto semi=n.rfind(';');if(semi!=std::string::npos){Type a;a.kind=TypeKind::Array;a.element=std::make_shared<Type>(typeFromName(n.substr(1,semi-1)));try{a.count=static_cast<std::size_t>(std::stoull(n.substr(semi+1,n.size()-semi-2),nullptr,0));}catch(...){a.count=0;}return a;}}if(n=="void")return simpleType(TypeKind::Void);if(n=="null")return simpleType(TypeKind::Null);if(n=="bool")return simpleType(TypeKind::Bool);if(n=="i8")return simpleType(TypeKind::I8);if(n=="i16")return simpleType(TypeKind::I16);if(n=="i32")return simpleType(TypeKind::I32);if(n=="i64")return simpleType(TypeKind::I64);if(n=="u8")return simpleType(TypeKind::U8);if(n=="u16")return simpleType(TypeKind::U16);if(n=="u32")return simpleType(TypeKind::U32);if(n=="u64")return simpleType(TypeKind::U64);if(n=="isize")return simpleType(TypeKind::Isize);if(n=="usize")return simpleType(TypeKind::Usize);if(n=="int"||n=="int64")return simpleType(TypeKind::Int);if(n=="float"||n=="float64")return simpleType(TypeKind::Float);if(n=="string")return simpleType(TypeKind::String);Type r;r.kind=TypeKind::Record;r.recordName=n;return r;}
bool canAssign(Type target,Type value){if(target.kind==TypeKind::Unknown||value.kind==TypeKind::Unknown||target.kind==TypeKind::Generic||value.kind==TypeKind::Generic)return true;if(target==value)return true;if(target.isInteger()&&value.isInteger()){const auto tb=integerBits(target.kind),vb=integerBits(value.kind);const bool ts=integerSigned(target.kind),vs=integerSigned(value.kind);if(ts==vs)return tb>=vb;if(ts&&!vs)return tb>vb;return false;}if(target.kind==TypeKind::Float&&value.isInteger())return true;if(target.kind==TypeKind::Pointer&&value.kind==TypeKind::Null)return true;if(target.kind==TypeKind::Pointer&&value.kind==TypeKind::Pointer&&target.pointee&&value.pointee)return target.pointee->kind==TypeKind::Void||value.pointee->kind==TypeKind::Void||*target.pointee==*value.pointee;return false;}
std::string readTextFile(const std::filesystem::path&path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("cannot open file: "+path.string());std::ostringstream out;out<<in.rdbuf();if(in.bad()||(in.fail()&&!in.eof()))throw std::runtime_error("cannot read file: "+path.string());return out.str();}
CompileResult compileSource(const std::string&source){return compileSource(source,CompileOptions{});}CompileResult compileSource(const std::string&source,const CompileOptions&options){Diagnostics d;d.registerSource(0,"<memory>",source);auto syntax=preprocessGenericSyntax(source);Lexer lexer(syntax.source,d);auto tokens=lexer.lex();if(d.hasErrors())return compileProgram({},std::move(d),options);Parser parser(std::move(tokens),d);auto program=parser.parse();applyGenericSyntax(program,syntax);return compileProgram(std::move(program),std::move(d),options);}CompileResult compileFile(const std::filesystem::path&path){return compileFile(path,CompileOptions{});}CompileResult compileFile(const std::filesystem::path&path,const CompileOptions&options){Diagnostics d;auto graph=std::make_shared<ModuleGraph>();if(!graph->load(path,d))return compileProgram({},std::move(d),options,graph);Program merged=graph->mergedProgram();return compileProgram(std::move(merged),std::move(d),options,std::move(graph));}
} // namespace noe
