#include "shader_backend.hpp"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
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
enum class Scalar { U32, F32, Bool, Vec2, Vec3, Vec4 };
struct Value { std::string code; Scalar kind; };
struct Symbol { std::string target; Scalar kind; };
using Scope = std::unordered_map<std::string, Symbol>;

std::optional<Scalar> scalarType(const std::string& name) {
    if(name=="u32")return Scalar::U32;
    if(name=="float")return Scalar::F32;
    if(name=="bool")return Scalar::Bool;
    if(name=="NqVec2")return Scalar::Vec2;
    if(name=="NqVec3")return Scalar::Vec3;
    if(name=="NqVec4")return Scalar::Vec4;
    return std::nullopt;
}
const char* wgslType(Scalar kind) {
    switch(kind) {
        case Scalar::U32:return "u32";
        case Scalar::F32:return "f32";
        case Scalar::Bool:return "bool";
        case Scalar::Vec2:return "vec2<f32>";
        case Scalar::Vec3:return "vec3<f32>";
        case Scalar::Vec4:return "vec4<f32>";
    }
    return "void";
}
// Exactly these intrinsic declarations are permitted. They are checked
// against imported Noqeri extern signatures before any WGSL is emitted.
struct Intrinsic {
    Scalar result;
    std::vector<Scalar> args;
    const char* wgsl;
    bool texture=false;
    bool sampler=false;
    bool component=false;
};
std::optional<Intrinsic> shaderIntrinsic(const std::string& name) {
    using S=Scalar;
    if(name=="nqVec2")return Intrinsic{S::Vec2,{S::F32,S::F32},"vec2<f32>"};
    if(name=="nqVec3")return Intrinsic{S::Vec3,{S::F32,S::F32,S::F32},"vec3<f32>"};
    if(name=="nqVec4")return Intrinsic{S::Vec4,{S::F32,S::F32,S::F32,S::F32},"vec4<f32>"};
    if(name=="nqVec2Add")return Intrinsic{S::Vec2,{S::Vec2,S::Vec2},"+"};
    if(name=="nqVec3Add")return Intrinsic{S::Vec3,{S::Vec3,S::Vec3},"+"};
    if(name=="nqVec4Add")return Intrinsic{S::Vec4,{S::Vec4,S::Vec4},"+"};
    if(name=="nqVec2Sub")return Intrinsic{S::Vec2,{S::Vec2,S::Vec2},"-"};
    if(name=="nqVec3Sub")return Intrinsic{S::Vec3,{S::Vec3,S::Vec3},"-"};
    if(name=="nqVec4Sub")return Intrinsic{S::Vec4,{S::Vec4,S::Vec4},"-"};
    if(name=="nqVec2Scale")return Intrinsic{S::Vec2,{S::Vec2,S::F32},"*"};
    if(name=="nqVec3Scale")return Intrinsic{S::Vec3,{S::Vec3,S::F32},"*"};
    if(name=="nqVec4Scale")return Intrinsic{S::Vec4,{S::Vec4,S::F32},"*"};
    if(name=="nqDot2")return Intrinsic{S::F32,{S::Vec2,S::Vec2},"dot"};
    if(name=="nqDot3")return Intrinsic{S::F32,{S::Vec3,S::Vec3},"dot"};
    if(name=="nqDot4")return Intrinsic{S::F32,{S::Vec4,S::Vec4},"dot"};
    if(name=="nqLength2")return Intrinsic{S::F32,{S::Vec2},"length"};
    if(name=="nqLength3")return Intrinsic{S::F32,{S::Vec3},"length"};
    if(name=="nqNormalize2")return Intrinsic{S::Vec2,{S::Vec2},"normalize"};
    if(name=="nqNormalize3")return Intrinsic{S::Vec3,{S::Vec3},"normalize"};
    if(name=="nqVec4Mix")return Intrinsic{S::Vec4,{S::Vec4,S::Vec4,S::F32},"mix"};
    if(name=="nqVec4Clamp")return Intrinsic{S::Vec4,{S::Vec4,S::F32,S::F32},"clamp"};
    if(name=="nqVec4R")return Intrinsic{S::F32,{S::Vec4},"x",false,false,true};
    if(name=="nqVec4G")return Intrinsic{S::F32,{S::Vec4},"y",false,false,true};
    if(name=="nqVec4B")return Intrinsic{S::F32,{S::Vec4},"z",false,false,true};
    if(name=="nqVec4A")return Intrinsic{S::F32,{S::Vec4},"w",false,false,true};
    if(name=="nqTextureSample")return Intrinsic{S::Vec4,{S::Vec2},"textureSample",true,true};
    if(name=="nqTextureLoad")return Intrinsic{S::Vec4,{S::U32,S::U32},"textureLoad",true,false};
    if(name=="nqTextureSize")return Intrinsic{S::Vec2,{},"textureDimensions",true,false};
    return std::nullopt;
}
bool allowedVectorRecord(const RecordStmt& record) {
    const std::string& name=record.name;
    std::size_t count=0;
    if(name=="NqVec2")count=2;
    else if(name=="NqVec3")count=3;
    else if(name=="NqVec4")count=4;
    else return false;
    if(record.fields.size()!=count||!record.genericParams.empty())return false;
    const std::string components="xyzw";
    for(std::size_t i=0;i<count;++i)
        if(record.fields[i].name!=components.substr(i,1) ||
           record.fields[i].typeName!="float")return false;
    return true;
}
bool shaderError(Diagnostics& d, Span span, const std::string& message) {
    d.error("NQR-GPU100",span,message);
    return false;
}
bool constantZero(const ExprPtr& value) {
    if(auto cast=std::dynamic_pointer_cast<CastExpr>(value))
        return constantZero(cast->value);
    if(auto unary=std::dynamic_pointer_cast<UnaryExpr>(value))
        if(unary->op==TokenKind::Plus||unary->op==TokenKind::Minus)
            return constantZero(unary->operand);
    if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(value)) {
        if(auto integer=std::get_if<std::int64_t>(&literal->value))return *integer==0;
        if(auto number=std::get_if<double>(&literal->value))return *number==0.0;
    }
    return false;
}
std::string u32Literal(std::uint32_t v) {return std::to_string(v)+"u";}
std::optional<std::string> f32Literal(double value) {
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())
        return std::nullopt;
    const float narrowed=static_cast<float>(value);
    if(!std::isfinite(narrowed))return std::nullopt;
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s<<std::setprecision(std::numeric_limits<float>::max_digits10)<<narrowed;
    std::string n=s.str();
    if(n.find_first_of(".eE")==std::string::npos)n+=".0";
    return n+"f";
}

// Shader compilation never trusts the spelling of source-level identifiers
// as WGSL identifiers. This prevents keyword clashes and code injection.
class ShaderEmitter {
public:
    ShaderEmitter(Diagnostics& d,Scalar result,bool fragment)
        :d_(d),returnType_(result),fragment_(fragment) {}
    bool usesTexture()const{return usesTexture_;}
    bool usesSampler()const{return usesSampler_;}
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
                         "shader locals require a typed vector or scalar initializer");
                auto type=scalarType(*local->annotation);
                if(!type)return shaderError(d_,stmt->span,
                         "shader local type must be u32, float, bool, NqVec2, NqVec3 or NqVec4");
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
               constantZero(binary->right)){
                shaderError(d_,binary->span,"division by a literal zero is not allowed");
                return std::nullopt;
            }
            return Value{"("+left->code+" "+sign+" "+right->code+")",
                        resultBool?Scalar::Bool:left->kind};
        }
        if(auto member=std::dynamic_pointer_cast<MemberExpr>(value)){
            auto base=expr(member->object,depth+1);
            if(!base)return std::nullopt;
            const std::string components="xyzw";
            std::size_t count=0;
            if(base->kind==Scalar::Vec2)count=2;
            if(base->kind==Scalar::Vec3)count=3;
            if(base->kind==Scalar::Vec4)count=4;
            if(count==0||member->member.size()!=1||
               components.substr(0,count).find(member->member)==std::string::npos) {
                shaderError(d_,member->span,"only valid vector components x,y,z,w are supported");
                return std::nullopt;
            }
            return Value{"("+base->code+"."+member->member+")",Scalar::F32};
        }
        if(auto call=std::dynamic_pointer_cast<CallExpr>(value)){
            auto name=std::dynamic_pointer_cast<NameExpr>(call->callee);
            if(!name){
                shaderError(d_,call->span,"shader intrinsics must be direct named calls");
                return std::nullopt;
            }
            const auto intrinsic=shaderIntrinsic(name->name);
            if(!intrinsic||call->args.size()!=intrinsic->args.size()){
                shaderError(d_,call->span,"unknown or malformed shader intrinsic call");
                return std::nullopt;
            }
            if(intrinsic->texture&&!fragment_){
                shaderError(d_,call->span,"2D sampled textures require fragment stage");
                return std::nullopt;
            }
            std::vector<Value> arguments;
            arguments.reserve(call->args.size());
            for(std::size_t i=0;i<call->args.size();++i){
                auto arg=expr(call->args[i],depth+1);
                if(!arg)return std::nullopt;
                if(arg->kind!=intrinsic->args[i]){
                    shaderError(d_,call->span,"shader intrinsic argument type mismatch");
                    return std::nullopt;
                }
                arguments.push_back(*arg);
            }
            std::string code;
            if(intrinsic->component){
                code="("+arguments[0].code+"."+intrinsic->wgsl+")";
            }else if(name->name=="nqTextureSample"){
                code="textureSample(nq_texture, nq_sampler, "+arguments[0].code+")";
            }else if(name->name=="nqTextureLoad"){
                code="textureLoad(nq_texture, vec2<i32>(i32("+arguments[0].code+"), i32("+
                     arguments[1].code+")), 0)";
            }else if(name->name=="nqTextureSize"){
                code="vec2<f32>(textureDimensions(nq_texture, 0))";
            }else if(name->name=="nqVec4Clamp"){
                code="clamp("+arguments[0].code+", vec4<f32>("+arguments[1].code+
                     "), vec4<f32>("+arguments[2].code+"))";
            }else if(intrinsic->wgsl[0]=='+'||intrinsic->wgsl[0]=='-'||
                     intrinsic->wgsl[0]=='*'){
                code="("+arguments[0].code+" "+intrinsic->wgsl+" "+arguments[1].code+")";
            }else {
                code=std::string(intrinsic->wgsl)+"(";
                for(std::size_t i=0;i<arguments.size();++i){
                    if(i)code+=", ";
                    code+=arguments[i].code;
                }
                code+=")";
            }
            if(intrinsic->texture){
                usesTexture_=true;
                usesSampler_=usesSampler_||intrinsic->sampler;
            }
            return Value{code,intrinsic->result};
        }
        shaderError(d_,value->span,
            "shader rejects calls, pointers, indexing, aggregate data and impure operations");
        return std::nullopt;
    }
    Diagnostics& d_;
    Scalar returnType_;
    bool fragment_=false;
    bool usesTexture_=false;
    bool usesSampler_=false;
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
        if(auto rec=std::dynamic_pointer_cast<RecordStmt>(statement)){
            if(!allowedVectorRecord(*rec))
                return shaderError(diagnostics,rec->span,
                    "shader records must be canonical NqVec2/NqVec3/NqVec4 float layouts");
            continue;
        }
        auto fn=std::dynamic_pointer_cast<FunctionStmt>(statement);
        if(fn&&fn->isExtern){
            const auto spec=shaderIntrinsic(fn->name);
            if(!spec||fn->isExport||fn->isForward||!fn->genericParams.empty()||
               !fn->returnType||scalarType(*fn->returnType)!=spec->result||
               fn->params.size()!=spec->args.size())
                return shaderError(diagnostics,fn->span,
                    "only correctly typed Noqeri GPU intrinsic externs are permitted");
            for(std::size_t i=0;i<fn->params.size();++i)
                if(!fn->params[i].annotation ||
                   scalarType(*fn->params[i].annotation)!=spec->args[i])
                    return shaderError(diagnostics,fn->span,"invalid GPU intrinsic signature");
            continue;
        }
        if(!fn||shader||!fn->isExport||fn->isExtern||fn->isForward||
           fn->name!="shade"||!fn->genericParams.empty())
            return shaderError(diagnostics,statement->span,
                 "shader source must contain exactly one exported shade function and approved GPU intrinsics");
        shader=fn.get();
    }
    const auto args=stage=="compute"?std::size_t{1}:std::size_t{2};
    if(!shader||!shader->returnType||shader->params.size()!=args||!shader->body)
        return shaderError(diagnostics,{},
            "expected export function shade(value: u32|float): u32|float "
            "or shade(x: u32|float, y: u32|float): u32|float");
    auto resultType=scalarType(*shader->returnType);
    if(!resultType||*resultType==Scalar::Bool||*resultType==Scalar::Vec2||
       *resultType==Scalar::Vec3)
        return shaderError(diagnostics,shader->span,"shader must return u32, float or NqVec4");
    ShaderEmitter emitter(diagnostics,*resultType,stage=="fragment");
    for(std::size_t i=0;i<shader->params.size();++i){
        const Scalar expected=stage=="fragment" && *resultType==Scalar::Vec4 ?
            Scalar::F32 : *resultType;
        if(!emitter.parameter(shader->params[i],i,expected))return false;
    }
    bool returns=false;
    if(!emitter.statements(shader->body->statements,1,returns)||!returns)
        return shaderError(diagnostics,shader->span,
             "shader must return on all control-flow paths");
    if(diagnostics.hasErrors())return false;

    std::ostringstream wgsl;
    wgsl<<"// Generated from typechecked Noqeri source; pure bounded shader profile.\n";
    wgsl<<"fn noqeri_shade("<<emitter.parameters(*shader)<<") -> "<<wgslType(*resultType)<<" {\n"
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
    }else if(*resultType==Scalar::Vec4){
        if(emitter.usesTexture()){
            wgsl<<"@group(0) @binding(0) var nq_texture: texture_2d<f32>;\n";
            if(emitter.usesSampler())
                wgsl<<"@group(0) @binding(1) var nq_sampler: sampler;\n";
        }
        wgsl<<"@fragment\n"
            <<"fn main(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {\n"
            <<"  return clamp(noqeri_shade(position.x, position.y), "
              "vec4<f32>(0.0), vec4<f32>(1.0));\n"
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
