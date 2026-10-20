#include "noe.hpp"
#include <fstream>
#include <sstream>

namespace noe {
namespace {
std::string jsString(const std::string&s){std::string out="\"";for(char c:s){switch(c){case'\\':out+="\\\\";break;case'\"':out+="\\\"";break;case'\n':out+="\\n";break;case'\r':out+="\\r";break;case'\t':out+="\\t";break;default:out+=c;}}return out+'"';}
std::string op(TokenKind k){switch(k){case TokenKind::Plus:return"+";case TokenKind::Minus:return"-";case TokenKind::Star:return"*";case TokenKind::Slash:return"/";case TokenKind::Percent:return"%";case TokenKind::Bang:return"!";case TokenKind::Equal:return"=";case TokenKind::EqualEqual:return"===";case TokenKind::BangEqual:return"!==";case TokenKind::Less:return"<";case TokenKind::LessEqual:return"<=";case TokenKind::Greater:return">";case TokenKind::GreaterEqual:return">=";case TokenKind::AndAnd:return"&&";case TokenKind::OrOr:return"||";default:return"?";}}
const char* runtimePrelude(){return R"NQRI(// Noqeri 1.0 portable web runtime. The generated .mjs file is a standard ES module.
const __nqHost = () => globalThis.NoqeriHost;
const __nqNeedHost = (name) => { const h=__nqHost(); if(!h || typeof h[name] !== 'function') throw new Error(`Noqeri host capability '${name}' is unavailable`); return h[name].bind(h); };
const __nqNonNull = (value,what='value') => { if(value === null || value === undefined) throw new Error(`Noqeri null check failed for ${what}`); return value; };
const __nqCheckedIndex = (value,index) => { __nqNonNull(value,'index base'); const length=Number(value.length ?? 0); if(!Number.isInteger(index) || index < 0 || index >= length) throw new RangeError(`Noqeri index out of bounds: index=${index}, length=${length}`); return index; };
const __nqIndex = (value,index) => value[__nqCheckedIndex(value,index)];
const __nqSetIndex = (value,index,next) => { value[__nqCheckedIndex(value,index)] = next; return next; };
export const jsonEncode = (value) => JSON.stringify(value);
export const jsonDecode = (text) => JSON.parse(text);
export const jsonValid = (text) => { try { JSON.parse(text); return true; } catch { return false; } };
export const urlEncode = (value) => encodeURIComponent(value);
export const urlDecode = (value) => decodeURIComponent(value);
export const htmlEscape = (value) => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export const routePath = () => globalThis.location?.pathname ?? '/';
export const routeGo = (path) => { if(!globalThis.location) throw new Error('routeGo requires a browser'); globalThis.history.pushState({},'',path); globalThis.dispatchEvent(new PopStateEvent('popstate')); };
export const domText = (selector,value) => { const node=document.querySelector(selector); if(!node) return false; node.textContent=String(value); return true; };
export const domHtml = (selector,value) => { const node=document.querySelector(selector); if(!node) return false; node.innerHTML=String(value); return true; };
export const domAttr = (selector,name,value) => { const node=document.querySelector(selector); if(!node) return false; node.setAttribute(name,String(value)); return true; };
export const domAddClass = (selector,name) => { const node=document.querySelector(selector); if(!node) return false; node.classList.add(name); return true; };
export const domRemoveClass = (selector,name) => { const node=document.querySelector(selector); if(!node) return false; node.classList.remove(name); return true; };
export const domToggleClass = (selector,name,force) => { const node=document.querySelector(selector); if(!node) return false; node.classList.toggle(name,Boolean(force)); return true; };
export const domStyle = (selector,name,value) => { const node=document.querySelector(selector); if(!node) return false; node.style.setProperty(String(name),String(value)); return true; };
export const domStyleAll = (selector,name,value) => { const nodes=document.querySelectorAll(selector); for(const node of nodes) node.style.setProperty(String(name),String(value)); return nodes.length; };
export const domRectJson = (selector) => { const node=document.querySelector(selector); if(!node) return ''; const r=node.getBoundingClientRect(); return JSON.stringify({x:r.x,y:r.y,width:r.width,height:r.height,top:r.top,right:r.right,bottom:r.bottom,left:r.left}); };
export const mediaReducedMotion = () => Boolean(globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches);
export const mediaFinePointer = () => Boolean(globalThis.matchMedia?.('(hover: hover) and (pointer: fine)').matches);
export const viewportWidth = () => Number(globalThis.innerWidth ?? 0);
export const viewportHeight = () => Number(globalThis.innerHeight ?? 0);
export const scrollYPosition = () => Number(globalThis.scrollY ?? globalThis.pageYOffset ?? 0);
export const motionNow = () => Number(globalThis.performance?.now?.() ?? Date.now());
export const motionProfile = () => JSON.stringify({reducedMotion:mediaReducedMotion(),finePointer:mediaFinePointer(),viewportWidth:viewportWidth(),viewportHeight:viewportHeight(),scrollY:scrollYPosition()});
let __nqFetchNext=1; const __nqFetch=new Map();
export const fetchStart = (url) => { const id=__nqFetchNext++; const state={ready:false,ok:false,text:'',status:0}; __nqFetch.set(id,state); fetch(url).then(async r=>{state.ok=r.ok;state.status=r.status;state.text=await r.text();state.ready=true;}).catch(e=>{state.text=String(e);state.ready=true;}); return id; };
export const fetchReady = (id) => __nqFetch.get(id)?.ready ?? false;
export const fetchOk = (id) => __nqFetch.get(id)?.ok ?? false;
export const fetchStatus = (id) => __nqFetch.get(id)?.status ?? 0;
export const fetchText = (id) => __nqFetch.get(id)?.text ?? '';
let __nqWsNext=1; const __nqWs=new Map();
export const websocketOpen = (url) => { const id=__nqWsNext++; const state={socket:new WebSocket(url),messages:[]}; state.socket.addEventListener('message',e=>state.messages.push(String(e.data))); __nqWs.set(id,state); return id; };
export const websocketSend = (id,text) => { const s=__nqWs.get(id)?.socket; if(!s) return false; s.send(text); return true; };
export const websocketMessage = (id) => __nqWs.get(id)?.messages.shift() ?? '';
export const websocketClose = (id) => { const s=__nqWs.get(id)?.socket; if(!s) return false; s.close(); __nqWs.delete(id); return true; };
export const httpRouteText = (method,path,status,body) => __nqNeedHost('httpRouteText')(method,path,status,body);
export const httpStatic = (prefix,directory) => __nqNeedHost('httpStatic')(prefix,directory);
export const httpListen = (port) => __nqNeedHost('httpListen')(port);
export const fsReadText = (path) => __nqNeedHost('fsReadText')(path);
export const fsWriteText = (path,text) => __nqNeedHost('fsWriteText')(path,text);
export const fsExists = (path) => __nqNeedHost('fsExists')(path);
export const envGet = (name) => __nqNeedHost('envGet')(name);
export const timeNowMillis = () => Date.now();
export const cryptoRandomHex = (bytes) => __nqNeedHost('cryptoRandomHex')(bytes);
export const cryptoSha256 = (text) => __nqNeedHost('cryptoSha256')(text);
export const dbOpen = (path) => __nqNeedHost('dbOpen')(path);
export const dbExec = (handle,script) => __nqNeedHost('dbExec')(handle,script);

)NQRI";}
class Emitter{
public:Emitter(Diagnostics&d):d_(d){}std::string run(const Program&p){out_<<"// Generated by Noqeri "<<NOQERI_COMPILER_VERSION<<" (.nqri).\n'use strict';\n\n"<<runtimePrelude();for(const auto&s:p.statements)stmt(s,0);return out_.str();}
private:
std::string expr(const ExprPtr&e){
    if(!e)return"undefined";
    if(auto x=std::dynamic_pointer_cast<LiteralExpr>(e)){if(std::holds_alternative<std::monostate>(x->value))return"null";if(auto v=std::get_if<std::int64_t>(&x->value))return std::to_string(*v);if(auto v=std::get_if<double>(&x->value)){std::ostringstream o;o<<*v;return o.str();}if(auto v=std::get_if<bool>(&x->value))return*v?"true":"false";return jsString(std::get<std::string>(x->value));}
    if(auto x=std::dynamic_pointer_cast<NameExpr>(e))return x->name;
    if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e)){if(x->op==TokenKind::Try||x->op==TokenKind::Ampersand||x->op==TokenKind::Star){d_.error("NQR-W7001",x->span,"web backend does not permit raw-memory/try unary operation in portable modules");return"undefined";}return"("+op(x->op)+expr(x->operand)+")";}
    if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){
        if(x->op==TokenKind::Equal){
            if(auto indexed=std::dynamic_pointer_cast<IndexExpr>(x->left))return"__nqSetIndex("+expr(indexed->object)+", "+expr(indexed->index)+", "+expr(x->right)+")";
            if(auto member=std::dynamic_pointer_cast<MemberExpr>(x->left))return"("+expr(member->object)+"."+member->member+" = "+expr(x->right)+")";
        }
        return"("+expr(x->left)+" "+op(x->op)+" "+expr(x->right)+")";
    }
    if(auto x=std::dynamic_pointer_cast<CastExpr>(e))return expr(x->value);
    if(auto x=std::dynamic_pointer_cast<ArrayExpr>(e)){std::string r="[";for(std::size_t i=0;i<x->elements.size();++i){if(i)r+=", ";r+=expr(x->elements[i]);}return r+"]";}
    if(auto x=std::dynamic_pointer_cast<IndexExpr>(e))return"__nqIndex("+expr(x->object)+", "+expr(x->index)+")";
    if(auto x=std::dynamic_pointer_cast<MemberExpr>(e))return expr(x->object)+"."+x->member;
    if(auto x=std::dynamic_pointer_cast<CallExpr>(e)){std::string name;if(auto n=std::dynamic_pointer_cast<NameExpr>(x->callee))name=n->name;if(name=="asm"||name=="intrinsic"||name.rfind("atomic",0)==0||name=="host"||name=="abi"){d_.error("NQR-W7002",x->span,"web module uses native-only capability '"+name+"'");return"undefined";}if(name=="print")name="console.log";std::string r=name.empty()?expr(x->callee):name;r+='(';for(std::size_t i=0;i<x->args.size();++i){if(i)r+=", ";r+=expr(x->args[i]);}return r+")";}
    d_.error("NQR-W7003",e->span,"unsupported expression in web backend");return"undefined";
}
void indent(int n){for(int i=0;i<n;++i)out_<<"    ";}
void stmt(const StmtPtr&s,int depth){if(!s)return;if(std::dynamic_pointer_cast<ModuleStmt>(s)||std::dynamic_pointer_cast<ImportStmt>(s)||std::dynamic_pointer_cast<RecordStmt>(s))return;if(auto x=std::dynamic_pointer_cast<FunctionStmt>(s)){if(x->isExtern)return;indent(depth);if(x->isExport)out_<<"export ";out_<<"function "<<x->name<<'(';for(std::size_t i=0;i<x->params.size();++i){if(i)out_<<", ";out_<<x->params[i].name;}out_<<") ";block(x->body,depth);out_<<"\n";return;}if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){indent(depth);out_<<(x->isConst?"const ":"let ")<<x->name<<" = "<<expr(x->initializer)<<";\n";return;}if(auto x=std::dynamic_pointer_cast<ExprStmt>(s)){indent(depth);out_<<expr(x->expr)<<";\n";return;}if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s)){indent(depth);out_<<"return"<<(x->value?" "+expr(x->value):"")<<";\n";return;}if(auto x=std::dynamic_pointer_cast<ThrowStmt>(s)){d_.error("NQR-W7004",x->span,"integer-status throw is native/reference-runtime only; use an explicit web Result value");return;}if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){block(x,depth);return;}if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){indent(depth);out_<<"if ("<<expr(x->condition)<<") ";emitBody(x->thenBranch,depth);if(x->elseBranch){indent(depth);out_<<"else ";emitBody(x->elseBranch,depth);}return;}if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){indent(depth);out_<<"while ("<<expr(x->condition)<<") ";emitBody(x->body,depth);return;}}
void block(const std::shared_ptr<BlockStmt>&b,int depth){out_<<"{\n";if(b)for(const auto&s:b->statements)stmt(s,depth+1);indent(depth);out_<<"}\n";}
void emitBody(const StmtPtr&s,int depth){if(auto b=std::dynamic_pointer_cast<BlockStmt>(s))block(b,depth);else{out_<<"{\n";stmt(s,depth+1);indent(depth);out_<<"}\n";}}
Diagnostics&d_;std::ostringstream out_;};
}
std::string WebBackend::emitModule(const Program&program,Diagnostics&diagnostics)const{return Emitter(diagnostics).run(program);}
bool WebBackend::emitModule(const Program&program,const std::filesystem::path&output,Diagnostics&diagnostics)const{auto text=emitModule(program,diagnostics);if(diagnostics.hasErrors())return false;std::ofstream out(output,std::ios::binary|std::ios::trunc);if(!out){diagnostics.error("NQR-W7000",{},"cannot write web module: "+output.string());return false;}out<<text;return static_cast<bool>(out);}
} // namespace noe
