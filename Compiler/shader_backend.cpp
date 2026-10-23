#include "shader_backend.hpp"
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

namespace noe {
namespace {
bool shaderError(Diagnostics& diagnostics,Span span,const std::string& description){
    diagnostics.error("NQR-GPU100",span,description);
    return false;
}
std::optional<std::string> expression(const ExprPtr& value,
       const std::unordered_set<std::string>& variables,Diagnostics& diagnostics,
       unsigned depth=0) {
    if(!value || depth>64){
        shaderError(diagnostics,{}, "shader expression nesting exceeds limit");
        return std::nullopt;
    }
    if(auto literal=std::dynamic_pointer_cast<LiteralExpr>(value)){
        auto integer=std::get_if<std::int64_t>(&literal->value);
        if(!integer||*integer<0||static_cast<std::uint64_t>(*integer)>
           std::numeric_limits<std::uint32_t>::max()){
            shaderError(diagnostics,value->span,"shader constants must be unsigned 32-bit integers");
            return std::nullopt;
        }
        return std::to_string(*integer)+"u";
    }
    if(auto name=std::dynamic_pointer_cast<NameExpr>(value)){
        if(!variables.count(name->name)){
            shaderError(diagnostics,value->span,"shader cannot access unknown variables or captured host state");
            return std::nullopt;
        }
        return name->name;
    }
    if(auto cast=std::dynamic_pointer_cast<CastExpr>(value)){
        if(cast->typeName!="u32"){
            shaderError(diagnostics,value->span,"shader subset supports only explicit u32 casts");
            return std::nullopt;
        }
        auto inner=expression(cast->value,variables,diagnostics,depth+1);
        return inner?std::optional<std::string>("u32("+*inner+")"):std::nullopt;
    }
    if(auto op=std::dynamic_pointer_cast<BinaryExpr>(value)){
        const char* sign=nullptr;
        switch(op->op){
            case TokenKind::Plus:sign="+";break;
            case TokenKind::Minus:sign="-";break;
            case TokenKind::Star:sign="*";break;
            case TokenKind::Slash:sign="/";break;
            case TokenKind::Percent:sign="%";break;
            default:break;
        }
        if(!sign){
            shaderError(diagnostics,value->span,"shader allows only pure u32 + - * / % operators");
            return std::nullopt;
        }
        auto left=expression(op->left,variables,diagnostics,depth+1);
        auto right=expression(op->right,variables,diagnostics,depth+1);
        if(!left||!right)return std::nullopt;
        return "("+*left+" "+sign+" "+*right+")";
    }
    shaderError(diagnostics,value->span,
        "shader rejects calls, pointers, loops, mutable state, FFI and side effects");
    return std::nullopt;
}
} // namespace

bool ShaderBackend::emitWgsl(const Program& program,const std::filesystem::path& output,
                              const std::string& stage,Diagnostics& diagnostics)const{
    if(stage!="compute"&&stage!="fragment")
        return shaderError(diagnostics,{},"shader stage must be compute or fragment");
    const FunctionStmt* shader=nullptr;
    for(const auto& statement:program.statements){
        if(std::dynamic_pointer_cast<ModuleStmt>(statement))continue;
        auto fn=std::dynamic_pointer_cast<FunctionStmt>(statement);
        if(!fn||shader||!fn->isExport||fn->isExtern||fn->isForward||
           fn->name!="shade"||!fn->genericParams.empty()){
            return shaderError(diagnostics,statement->span,
               "shader source must contain exactly one exported shade function and no host declarations");
        }
        shader=fn.get();
    }
    const std::size_t args=stage=="compute"?1u:2u;
    if(!shader||!shader->returnType||*shader->returnType!="u32"||
       shader->params.size()!=args||!shader->body||
       shader->body->statements.size()!=1u){
        return shaderError(diagnostics,{},
           "compute expects export function shade(value: u32): u32; "
           "fragment expects export function shade(x: u32, y: u32): u32");
    }
    std::unordered_set<std::string> vars;
    for(const auto& param:shader->params){
        if(!param.annotation||*param.annotation!="u32"||
           !vars.insert(param.name).second)
            return shaderError(diagnostics,param.span,
                               "shader parameters must be unique u32 values");
    }
    auto statement=std::dynamic_pointer_cast<ReturnStmt>(shader->body->statements[0]);
    if(!statement||!statement->value)return shaderError(diagnostics,shader->span,
                                   "shader body must be one pure return expression");
    auto body=expression(statement->value,vars,diagnostics);
    if(!body||diagnostics.hasErrors())return false;
    std::ostringstream wgsl;
    wgsl<<"// Generated from typed Noqeri shader source. Do not edit this output.\n"
        <<"// Strict unsigned integer shader profile; Noqeri-owned source is canonical.\n";
    wgsl<<"fn noqeri_shade(";
    for(std::size_t i=0;i<shader->params.size();++i){
        if(i)wgsl<<", ";
        wgsl<<shader->params[i].name<<": u32";
    }
    wgsl<<") -> u32 { return "<<*body<<"; }\n";
    if(stage=="compute"){
        wgsl<<"@group(0) @binding(0) var<storage, read> input_pixels: array<u32>;\n"
            <<"@group(0) @binding(1) var<storage, read_write> output_pixels: array<u32>;\n"
            <<"@compute @workgroup_size(64)\n"
            <<"fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
            <<"  let i = id.x;\n"
            <<"  if (i >= arrayLength(&input_pixels) || i >= arrayLength(&output_pixels)) { return; }\n"
            <<"  output_pixels[i] = noqeri_shade(input_pixels[i]);\n"
            <<"}\n";
    }else{
        wgsl<<"@fragment\n"
            <<"fn main(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {\n"
            <<"  let p = noqeri_shade(u32(position.x), u32(position.y));\n"
            <<"  let r = f32((p >> 16u) & 255u) / 255.0;\n"
            <<"  let g = f32((p >> 8u) & 255u) / 255.0;\n"
            <<"  let b = f32(p & 255u) / 255.0;\n"
            <<"  return vec4<f32>(r, g, b, 1.0);\n"
            <<"}\n";
    }
    std::ofstream out(output,std::ios::binary|std::ios::trunc);
    if(!out)return shaderError(diagnostics,{},"cannot open shader output for writing");
    out<<wgsl.str();
    if(!out)return shaderError(diagnostics,{},"failed writing WGSL output");
    return !diagnostics.hasErrors();
}
} // namespace noe
