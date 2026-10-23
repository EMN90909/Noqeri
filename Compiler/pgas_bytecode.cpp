#include "pgas_bytecode.hpp"
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace noe {
namespace {
using Captures=std::unordered_map<std::string,std::int64_t>;
std::optional<std::int64_t> smallInteger(const ExprPtr &value) {
    if(auto cast=std::dynamic_pointer_cast<CastExpr>(value)) {
        if(cast->typeName!="i64"&&cast->typeName!="int")return std::nullopt;
        return smallInteger(cast->value);
    }
    if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(value))
        if(auto n=std::get_if<std::int64_t>(&literal->value))
            if(*n>=-128 && *n<=127)return *n;
    if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(value))
        if(unary->op==TokenKind::Minus) {
            auto n=smallInteger(unary->operand);
            if(n && -*n<=127 && -*n>=-128)return -*n;
        }
    return std::nullopt;
}
class Emitter {
public:
    Emitter(const Captures& globals,std::string parameter,Diagnostics& diagnostics)
     :captures_(globals),parameter_(std::move(parameter)),diagnostics_(diagnostics){}
    bool emit(const ExprPtr& expression) {
        if(!expression)return failure({},"missing expression");
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(expression)){
            if(cast->typeName!="i64"&&cast->typeName!="int")
                return failure(cast->span,"only i64 casts are supported");
            return emit(cast->value);
        }
        if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(expression)){
            auto n=smallInteger(expression);
            if(!n)return failure(literal->span,"remote constants must be signed 8-bit integers");
            return push(*n);
        }
        if(auto name=std::dynamic_pointer_cast<NameExpr>(expression)){
            if(name->name==parameter_)return op(1,0,1);
            auto capture=captures_.find(name->name);
            if(capture==captures_.end())
                return failure(name->span,"remote code cannot capture mutable, unknown or private state");
            return push(capture->second);
        }
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(expression)) {
            if(unary->op!=TokenKind::Minus)
                return failure(unary->span,"unsupported unary operation in pure remote closure");
            return push(0) && emit(unary->operand) && op(4,2,1);
        }
        if(auto binary=std::dynamic_pointer_cast<BinaryExpr>(expression)){
            unsigned opcode=0;
            switch(binary->op){
                case TokenKind::Plus:opcode=3;break;
                case TokenKind::Minus:opcode=4;break;
                case TokenKind::Star:opcode=5;break;
                case TokenKind::Slash:opcode=6;break;
                default:return failure(binary->span,
                    "remote pure closures support only +, -, *, / expressions");
            }
            return emit(binary->left) && emit(binary->right) && op(opcode,2,1);
        }
        return failure(expression->span,
            "remote closure contains unsupported call, memory access, loop or side effect");
    }
    bool finish() {return op(14,1,0);}
    const std::vector<std::uint8_t>& bytes()const {return bytes_;}
private:
    bool failure(Span span,const std::string& message) {
        diagnostics_.error("NQR-PGAS500",span,message);
        return false;
    }
    bool op(unsigned opcode,unsigned pop,unsigned pushCount){
        if(depth_<pop)return failure({},"PGAS compiler stack underflow");
        depth_=depth_-pop+pushCount;
        if(depth_>32||bytes_.size()+1>256)
            return failure({},"PGAS bytecode exceeds 32 stack slots or 256 bytes");
        bytes_.push_back(static_cast<std::uint8_t>(opcode));
        return true;
    }
    bool push(std::int64_t n){
        if(n<-128||n>127)return failure({},"capture/constant does not fit signed 8-bit immediate");
        if(!op(2,0,1)||bytes_.size()+1>256)return false;
        bytes_.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(n)));
        return true;
    }
    const Captures& captures_;
    std::string parameter_;
    Diagnostics& diagnostics_;
    std::vector<std::uint8_t> bytes_;
    std::size_t depth_=0;
};
}

bool PgasBytecodeBackend::emitModule(const Program& p,const std::filesystem::path& output,
                                    Diagnostics& diagnostics) const {
    Captures captures;
    const FunctionStmt* fn=nullptr;
    for(const auto& stmt:p.statements){
        if(std::dynamic_pointer_cast<ImportStmt>(stmt) ||
           std::dynamic_pointer_cast<ModuleStmt>(stmt)) {
            diagnostics.error("NQR-PGAS501",stmt->span,
                "remote closure compilation is intentionally limited to a self-contained source file");
            return false;
        }
        if(auto var=std::dynamic_pointer_cast<LetStmt>(stmt)){
            if(!var->isConst||var->partition||!var->annotation ||
               (*var->annotation!="i64"&&*var->annotation!="int")){
                diagnostics.error("NQR-PGAS502",var->span,
                    "captures must be module-scope const i64 compile-time literals");
                return false;
            }
            auto value=smallInteger(var->initializer);
            if(!value||!captures.emplace(var->name,*value).second){
                diagnostics.error("NQR-PGAS503",var->span,
                    "captured constant must be unique and fit signed 8-bit range");
                return false;
            }
            continue;
        }
        if(auto f=std::dynamic_pointer_cast<FunctionStmt>(stmt)){
            if(fn||!f->isExport||f->isExtern||f->isForward||
               f->params.size()!=1 || !f->params[0].annotation ||
               *f->params[0].annotation!="i64" || !f->returnType ||
               *f->returnType!="i64" || !f->genericParams.empty()){
                diagnostics.error("NQR-PGAS504",f->span,
                    "exactly one export function remote(arg: i64): i64 is supported");
                return false;
            }
            fn=f.get();continue;
        }
        diagnostics.error("NQR-PGAS505",stmt->span,
            "remote closure module must contain only const captures and one exported function");
        return false;
    }
    if(!fn || fn->name!="remote" || !fn->body || fn->body->statements.size()!=1){
        diagnostics.error("NQR-PGAS506",{},
            "expected exactly one exported remote(arg: i64): i64 with a single return expression");
        return false;
    }
    auto ret=std::dynamic_pointer_cast<ReturnStmt>(fn->body->statements[0]);
    if(!ret||!ret->value){
        diagnostics.error("NQR-PGAS507",fn->span,
            "remote closure must return one pure arithmetic expression");
        return false;
    }
    Emitter emitter(captures,fn->params[0].name,diagnostics);
    if(!emitter.emit(ret->value)||!emitter.finish())return false;
    std::ofstream out(output,std::ios::binary|std::ios::trunc);
    if(!out) {
        diagnostics.error("NQR-PGAS508",{},"cannot open PGAS bytecode destination");
        return false;
    }
    const auto& bytes=emitter.bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if(!out) {
        diagnostics.error("NQR-PGAS509",{},"failed writing PGAS bytecode");
        return false;
    }
    return !diagnostics.hasErrors();
}
} // namespace noe
