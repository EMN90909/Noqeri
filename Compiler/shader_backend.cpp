#include "shader_backend.hpp"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace noe {
namespace {

// Noqeri currently calls its IEEE floating point type "float". The shader
// profile intentionally narrows it to WebGPU f32 and documents this ABI.
enum class Scalar { U32, F32, Bool };
struct Value { std::string code; Scalar kind; };
struct Symbol { std::string target; Scalar kind; };
using Scope = std::unordered_map<std::string, Symbol>;

std::optional<Scalar> scalarType(const std::string& name) {
    if(name=="u32")return Scalar::U32;
    if(name=="float")return Scalar::F32;
    if(name=="bool")return Scalar::Bool;
    return std::nullopt;
}
const char* wgslType(Scalar kind) {
    switch(kind) {
        case Scalar::U32:return "u32";
        case Scalar::F32:return "f32";
        case Scalar::Bool:return "bool";
    }
    return "void";
}
bool shaderError(Diagnostics& d, Span span, const std::string& message) {
    d.error("NQR-GPU100",span,message);
    return false;
}
std::string u32Literal(std::uint32_t v) {return std::to_string(v)+"u";}
std::optional<std::string> f32Literal(double value) {
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())
        return std::nullopt;
    const float narrowed=static_cast<float>(value);
    if(!std::isfinite(narrowed))return std::nullopt;
    std::ostringstream s;
    s<<std::setprecision(std::numeric_limits<float>::max_digits10)<<narrowed;
    std::string n=s.str();
    if(n.find_first_of(".eE")==std::string::npos)n+=".0";
    return n+"f";
}

// Shader compilation never trusts the spelling of source-level identifiers
// as WGSL identifiers. This prevents keyword clashes and code injection.
class ShaderEmitter {
public:
    ShaderEmitter(Diagnostics& d,Scalar result):d_(d),returnType_(result) {}
    bool parameter(const Parameter& p,std::size_t index,Scalar kind) {
        if(!p.annotation||scalarType(*p.annotation)!=kind)
            return shaderError(d_,p.span,"shader parameter has an unsupported or mismatched type");
        if(!scopes_.back().emplace(p.name,Symbol{"nq_arg"+std::to_string(index),kind}).second)
            return shaderError(d_,p.span,"duplicate shader parameter");
        return true;
    }
    std::string parameters(const FunctionStmt& fn)const{
        std::string out;
        for(std::size_t i=0;i<fn.params.size();++i){
            if(i)out+=", ";
            out+="nq_arg"+std::to_string(i)+": "+wgslType(*scalarType(*fn.params[i].annotation));
        }
        return out;
    }
    bool statements(const std::vector<StmtPtr>& stmts,unsigned depth,bool& returns) {
        if(depth>16||statements_+stmts.size()>256)
            return shaderError(d_,{},"shader exceeds the bounded statement or branch depth limit");
        statements_+=stmts.size();
        returns=false;
        for(const auto& stmt:stmts){
            if(returns)return shaderError(d_,stmt->span,"unreachable code after shader return");
            if(auto local=std::dynamic_pointer_cast<LetStmt>(stmt)) {
                if(!local->annotation||!local->initializer||local->partition||local->zeroInitialized)
                    return shaderError(d_,stmt->span,
                         "shader locals require a scalar type and an initializer");
                auto type=scalarType(*local->annotation);
                if(!type)return shaderError(d_,stmt->span,
                         "shader local type must be u32, float or bool");
                auto expression=expr(local->initializer);
                if(!expression||expression->kind!=*type)
                    return shaderError(d_,stmt->span,"shader local initializer type mismatch");
                auto& current=scopes_.back();
                const auto target="nq_local"+std::to_string(locals_++);
                if(!current.emplace(local->name,Symbol{target,*type}).second)
                    return shaderError(d_,stmt->span,"duplicate shader local variable");
                body_<<indent(depth)<<"let "<<target<<": "<<wgslType(*type)
                     <<" = "<<expression->code<<";\n";
                continue;
            }
            if(auto ret=std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
                auto value=expr(ret->value);
                if(!value||value->kind!=returnType_)
                    return shaderError(d_,stmt->span,"shader return type does not match stage signature");
                body_<<indent(depth)<<"return "<<value->code<<";\n";
                returns=true;
                continue;
            }
            if(auto branch=std::dynamic_pointer_cast<IfStmt>(stmt)){
                auto cond=expr(branch->condition);
                if(!cond||cond->kind!=Scalar::Bool)
                    return shaderError(d_,stmt->span,"shader if condition must have type bool");
                body_<<indent(depth)<<"if ("<<cond->code<<") {\n";
                bool thenReturns=false;
                if(!statementBranch(branch->thenBranch,depth+1,thenReturns))return false;
                body_<<indent(depth)<<"}";
                bool elseReturns=false;
                if(branch->elseBranch){
                    body_<<" else {\n";
                    if(!statementBranch(branch->elseBranch,depth+1,elseReturns))return false;
                    body_<<indent(depth)<<"}";
                }
                body_<<"\n";
                returns=branch->elseBranch&&thenReturns&&elseReturns;
                continue;
            }
            if(auto block=std::dynamic_pointer_cast<BlockStmt>(stmt)) {
                if(block->isUnsafe)
                    return shaderError(d_,stmt->span,"unsafe shader blocks are prohibited");
                body_<<indent(depth)<<"{\n";
                pushScope();
                bool blockReturns=false;
                const bool okay=statements(block->statements,depth+1,blockReturns);
                popScope();
                if(!okay)return false;
                body_<<indent(depth)<<"}\n";
                returns=blockReturns;
                continue;
            }
            return shaderError(d_,stmt->span,
                "shader supports only scalar locals, pure if/else and returns; no loops, writes or host calls");
        }
        return true;
    }
    std::string result()const{return body_.str();}
    bool valid()const{return !d_.hasErrors();}
    void pushScope(){scopes_.emplace_back();}
    void popScope(){scopes_.pop_back();}
private:
    std::string indent(unsigned depth)const{return std::string(depth*2,' ');}
    bool statementBranch(const StmtPtr& statement,unsigned depth,bool& returns){
        if(!statement)return shaderError(d_,{},"missing shader branch");
        pushScope();
        bool okay=false;
        if(auto block=std::dynamic_pointer_cast<BlockStmt>(statement)){
            if(block->isUnsafe){
                popScope();return shaderError(d_,block->span,"unsafe shader blocks are prohibited");
            }
            okay=statements(block->statements,depth,returns);
        }else okay=statements({statement},depth,returns);
        popScope();
        return okay;
    }
    std::optional<Value> expr(const ExprPtr& value,unsigned depth=0){
        if(!value||depth>64||++expressions_>1024){
            shaderError(d_,{}, "shader expression too deep or too large");
            return std::nullopt;
        }
        if(auto lit=std::dynamic_pointer_cast<LiteralExpr>(value)){
            if(auto integer=std::get_if<std::int64_t>(&lit->value)){
                if(*integer<0||static_cast<std::uint64_t>(*integer)>UINT32_MAX){
                    shaderError(d_,lit->span,"integer shader constants must fit u32");
                    return std::nullopt;
                }
                return Value{u32Literal(static_cast<std::uint32_t>(*integer)),Scalar::U32};
            }
            if(auto number=std::get_if<double>(&lit->value)){
                auto text=f32Literal(*number);
                if(!text){
                    shaderError(d_,lit->span,"float shader constant must be finite and fit f32");
                    return std::nullopt;
                }
                return Value{*text,Scalar::F32};
            }
            if(auto boolean=std::get_if<bool>(&lit->value))
                return Value{*boolean?"true":"false",Scalar::Bool};
            shaderError(d_,lit->span,"strings and nulls cannot be GPU scalar constants");
            return std::nullopt;
        }
        if(auto name=std::dynamic_pointer_cast<NameExpr>(value)){
            for(auto it=scopes_.rbegin();it!=scopes_.rend();++it){
                auto v=it->find(name->name);
                if(v!=it->end())return Value{v->second.target,v->second.kind};
            }
            shaderError(d_,name->span,"shader cannot reference host globals or unknown names");
            return std::nullopt;
        }
        if(auto cast=std::dynamic_pointer_cast<CastExpr>(value)){
            auto target=scalarType(cast->typeName);
            auto inside=expr(cast->value,depth+1);
            if(!target||!inside||*target==Scalar::Bool||inside->kind==Scalar::Bool){
                shaderError(d_,cast->span,"shader casts only support u32 and float");
                return std::nullopt;
            }
            return Value{std::string(wgslType(*target))+"("+inside->code+")",*target};
        }
        if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(value)){
            auto operand=expr(unary->operand,depth+1);
            if(!operand)return std::nullopt;
            if(unary->op==TokenKind::Bang&&operand->kind==Scalar::Bool)
                return Value{"(!"+operand->code+")",Scalar::Bool};
            if(unary->op==TokenKind::Plus&&operand->kind!=Scalar::Bool)return operand;
            if(unary->op==TokenKind::Minus&&operand->kind==Scalar::F32)
                return Value{"(-"+operand->code+")",Scalar::F32};
            shaderError(d_,unary->span,"unary shader operator incompatible with scalar type");
            return std::nullopt;
        }
        if(auto binary=std::dynamic_pointer_cast<BinaryExpr>(value)){
            if(binary->op==TokenKind::Equal){
                shaderError(d_,binary->span,"shader assignments are not allowed");
                return std::nullopt;
            }
            auto left=expr(binary->left,depth+1);
            auto right=expr(binary->right,depth+1);
            if(!left||!right)return std::nullopt;
            if(left->kind!=right->kind){
                shaderError(d_,binary->span,
                            "shader arithmetic/comparisons require equal scalar types; use an explicit cast");
                return std::nullopt;
            }
            const char* sign=nullptr;
            bool resultBool=false;
            switch(binary->op){
                case TokenKind::Plus:sign="+";break;
                case TokenKind::Minus:sign="-";break;
                case TokenKind::Star:sign="*";break;
                case TokenKind::Slash:sign="/";break;
                case TokenKind::Percent:sign="%";break;
                case TokenKind::EqualEqual:sign="==";resultBool=true;break;
                case TokenKind::BangEqual:sign="!=";resultBool=true;break;
                case TokenKind::Less:sign="<";resultBool=true;break;
                case TokenKind::LessEqual:sign="<=";resultBool=true;break;
                case TokenKind::Greater:sign=">";resultBool=true;break;
                case TokenKind::GreaterEqual:sign=">=";resultBool=true;break;
                case TokenKind::AndAnd:sign="&&";resultBool=true;break;
                case TokenKind::OrOr:sign="||";resultBool=true;break;
                default:break;
            }
            if(!sign){
                shaderError(d_,binary->span,"unsupported WGSL shader operator");
                return std::nullopt;
            }
            const bool logical=binary->op==TokenKind::AndAnd||binary->op==TokenKind::OrOr;
            const bool equality=binary->op==TokenKind::EqualEqual||binary->op==TokenKind::BangEqual;
            if((logical&&left->kind!=Scalar::Bool) ||
               (!logical&&!equality&&left->kind==Scalar::Bool) ||
               ((binary->op==TokenKind::Less||binary->op==TokenKind::LessEqual||
                 binary->op==TokenKind::Greater||binary->op==TokenKind::GreaterEqual)&&
                 left->kind==Scalar::Bool) ||
               (binary->op==TokenKind::Percent&&left->kind==Scalar::F32)){
                shaderError(d_,binary->span,"shader operation invalid for WGSL scalar type");
                return std::nullopt;
            }
            if((binary->op==TokenKind::Slash||binary->op==TokenKind::Percent)&&
               (right->code=="0u"||right->code=="0.0f")){
                shaderError(d_,binary->span,"division by a literal zero is not allowed");
                return std::nullopt;
            }
            return Value{"("+left->code+" "+sign+" "+right->code+")",
                        resultBool?Scalar::Bool:left->kind};
        }
        shaderError(d_,value->span,
            "shader rejects calls, pointers, indexing, aggregate data and impure operations");
        return std::nullopt;
    }
    Diagnostics& d_;
    Scalar returnType_;
    std::vector<Scope> scopes_{Scope{}};
    std::ostringstream body_;
    std::size_t statements_=0,expressions_=0,locals_=0;
};
} // namespace

bool ShaderBackend::emitWgsl(const Program& program,
                             const std::filesystem::path& output,
                             const std::string& stage,Diagnostics& diagnostics,
                             std::uint32_t workgroupSize)const{
    if(stage!="compute"&&stage!="fragment")
        return shaderError(diagnostics,{},"shader stage must be compute or fragment");
    if(workgroupSize!=32&&workgroupSize!=64&&workgroupSize!=128&&workgroupSize!=256)
        return shaderError(diagnostics,{},"workgroup size must be 32, 64, 128 or 256");

    const FunctionStmt* shader=nullptr;
    for(const auto& statement:program.statements){
        if(std::dynamic_pointer_cast<ModuleStmt>(statement))continue;
        auto fn=std::dynamic_pointer_cast<FunctionStmt>(statement);
        if(!fn||shader||!fn->isExport||fn->isExtern||fn->isForward||
           fn->name!="shade"||!fn->genericParams.empty())
            return shaderError(diagnostics,statement->span,
                 "shader source must contain exactly one exported pure shade function");
        shader=fn.get();
    }
    const auto args=stage=="compute"?std::size_t{1}:std::size_t{2};
    if(!shader||!shader->returnType||shader->params.size()!=args||!shader->body)
        return shaderError(diagnostics,{},
            "expected export function shade(value: u32|float): u32|float "
            "or shade(x: u32|float, y: u32|float): u32|float");
    auto resultType=scalarType(*shader->returnType);
    if(!resultType||*resultType==Scalar::Bool)
        return shaderError(diagnostics,shader->span,"shader must return u32 or float");
    ShaderEmitter emitter(diagnostics,*resultType);
    for(std::size_t i=0;i<shader->params.size();++i)
        if(!emitter.parameter(shader->params[i],i,*resultType))return false;
    bool returns=false;
    if(!emitter.statements(shader->body->statements,1,returns)||!returns)
        return shaderError(diagnostics,shader->span,
             "shader must return on all control-flow paths");
    if(diagnostics.hasErrors())return false;

    std::ostringstream wgsl;
    wgsl<<"// Generated from typechecked Noqeri source; pure bounded shader profile.\n";
    wgsl<<"fn noqeri_shade("<<emitter.parameters()<<") -> "<<wgslType(*resultType)<<" {\n"
        <<emitter.result()<<"}\n";
    if(stage=="compute"){
        const auto type=wgslType(*resultType);
        wgsl<<"@group(0) @binding(0) var<storage, read> input_pixels: array<"<<type<<">;\n"
            <<"@group(0) @binding(1) var<storage, read_write> output_pixels: array<"<<type<<">;\n"
            <<"@compute @workgroup_size("<<workgroupSize<<")\n"
            <<"fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
            <<"  let i = id.x;\n"
            <<"  if (i >= arrayLength(&input_pixels) || i >= arrayLength(&output_pixels)) { return; }\n"
            <<"  output_pixels[i] = noqeri_shade(input_pixels[i]);\n"
            <<"}\n";
    }else if(*resultType==Scalar::U32){
        wgsl<<"@fragment\n"
            <<"fn main(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {\n"
            <<"  let p = noqeri_shade(u32(position.x), u32(position.y));\n"
            <<"  let r = f32((p >> 16u) & 255u) / 255.0;\n"
            <<"  let g = f32((p >> 8u) & 255u) / 255.0;\n"
            <<"  let b = f32(p & 255u) / 255.0;\n"
            <<"  return vec4<f32>(r, g, b, 1.0);\n"
            <<"}\n";
    }else{
        wgsl<<"@fragment\n"
            <<"fn main(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {\n"
            <<"  let grayscale = clamp(noqeri_shade(position.x, position.y), 0.0, 1.0);\n"
            <<"  return vec4<f32>(grayscale, grayscale, grayscale, 1.0);\n"
            <<"}\n";
    }
    const std::string result=wgsl.str();
    if(result.size()>131072)
        return shaderError(diagnostics,shader->span,"generated WGSL exceeds 128 KiB");
    std::ofstream out(output,std::ios::binary|std::ios::trunc);
    if(!out)return shaderError(diagnostics,{},"cannot open shader output for writing");
    out<<result;
    if(!out)return shaderError(diagnostics,{},"failed writing WGSL output");
    return true;
}
} // namespace noe
