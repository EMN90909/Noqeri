// Noqeri web view subset: additive language experiment, NOT the Stage-2 typed compiler.
// Source is parsed to a bounded AST and emitted as standard browser ES modules.
// No eval/Function, raw HTML insertion, implicit npm resolution, or arbitrary globals.
const BAD = new Set(['__proto__','prototype','constructor','eval','Function','globalThis','window','document','process','require','new','this','await','yield','class','import','export','super','location','localStorage','sessionStorage']);
const TAGS = new Set('a abbr article aside b blockquote br button caption code dd details div dl dt em fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 header hr i img input label li main nav ol option p picture pre section select small span strong summary table tbody td textarea th thead time tr ul'.split(' '));
const VOID = new Set('br hr img input'.split(' '));
const EVENTS = new Set(['click','input','change','submit']);
const ATTRS = new Set(['id','class','title','role','href','src','alt','type','name','value','placeholder','for','disabled','checked','required','minlength','maxlength','min','max','step','autocomplete','inputmode','target','rel','tabindex']);
const THEME_KEYS = new Set(['primary','background','surface','text','muted','border','accent','radius']);
const OPS = {'||':1,'&&':2,'==':3,'!=':3,'<':4,'>':4,'<=':4,'>=':4,'+':5,'-':5,'*':6,'/':6,'%':6};
const isIdent = x => /^[A-Za-z_][A-Za-z_0-9]*$/.test(x) && !BAD.has(x);
export function isViewSource(source) {
  return /(?:^|\n)\s*(?:view\s*\{|component\s+[A-Za-z_]|on\.mount\b|import\s+js\s+|function\s+[A-Za-z_]|load\s+[A-Za-z_]|subscribe\s+[A-Za-z_]|persist\s+[A-Za-z_]|route\s+\")/m.test(source);
}
function tokenize(source) {
  if (typeof source !== 'string' || source.length > 1024 * 1024) throw Error('Noqeri view: source must be at most 1 MiB');
  const out=[];let i=0,line=1,col=1;
  const add=(kind,value,atLine,atCol)=>{if(out.length>=100000)throw Error('Noqeri view: token limit exceeded');out.push({kind,value,line:atLine,col:atCol})};
  while(i<source.length){
    const atLine=line,atCol=col;
    const skip=part=>{for(const ch of part){if(ch==='\n'){line++;col=1}else col++}i+=part.length};
    if(/\s/.test(source[i])){skip(source[i]);continue}
    if(source.startsWith('//',i)){skip(source.slice(i,(source.indexOf('\n',i)+1||source.length+1)-1));continue}
    if(source.startsWith('/*',i)){const end=source.indexOf('*/',i+2);if(end<0)throw Error('Unterminated comment at '+line+':'+col);skip(source.slice(i,end+2));continue}
    if(source[i]==='"'){
      let j=i+1,escaped=false;
      for(;j<source.length;j++){const ch=source[j];if(ch==='\n'&&!escaped)throw Error('Unterminated string at '+line+':'+col);if(!escaped&&ch==='"')break;if(ch==='\\'&&!escaped)escaped=true;else escaped=false}
      if(j>=source.length)throw Error('Unterminated string at '+line+':'+col);
      const raw=source.slice(i,j+1);let value;
      try{value=JSON.parse(raw)}catch{throw Error('Invalid JSON string at '+line+':'+col)}
      add('str',value,atLine,atCol);skip(raw);continue;
    }
    const m=/^[A-Za-z_][A-Za-z_0-9]*|^\d+(?:\.\d+)?|^(?:==|!=|>=|<=|&&|\|\|)|^[{}()[\].,:;=+\-*\/%!<>]/.exec(source.slice(i));
    if(!m)throw Error('Unsupported view token at '+line+':'+col);
    const value=m[0];add(/^\d/.test(value)?'num':/^[A-Za-z_]/.test(value)?'id':'op',value,atLine,atCol);skip(value);
  }
  add('eof','<eof>',line,col);return out;
}
export function parseView(source) {
  const tokens=tokenize(source);let at=0;
  const peek=(offset=0)=>tokens[at+offset]??tokens.at(-1),is=x=>peek().value===x;
  const fail=msg=>{throw Error('Noqeri view: '+msg+' at '+peek().line+':'+peek().col)};
  const take=()=>tokens[at++],accept=x=>is(x)?(at++,true):false;
  const need=x=>{if(!accept(x))fail('expected '+x+', got '+peek().value)};
  const ident=()=>{const t=take();if(t.kind!=='id'||!isIdent(t.value))fail('invalid identifier '+t.value);return t.value};
  let depth=0;
  const nest=fn=>{if(++depth>64)fail('nesting exceeds 64');try{return fn()}finally{depth--}};
  function expr(min=0) {
    return nest(()=>{
      let left;
      if(accept('!'))left={kind:'unary',op:'!',right:expr(7)};
      else if(accept('-'))left={kind:'unary',op:'-',right:expr(7)};
      else if(accept('(')){left=expr();need(')')}
      else if(accept('{')){
        const entries=[];
        while(!is('}')){
          const key=ident();need(':');entries.push([key,expr()]);
          if(!accept(',')){if(!is('}'))fail('expected comma in object')}
        }
        need('}');left={kind:'object',entries};
      } else {
        const t=take();
        if(t.kind==='str'||t.kind==='num')left={kind:'literal',value:t.kind==='num'?Number(t.value):t.value};
        else if(t.value==='true'||t.value==='false'||t.value==='null')left={kind:'literal',value:t.value==='null'?null:t.value==='true'};
        else if(t.kind==='id'&&isIdent(t.value))left={kind:'name',name:t.value};
        else fail('expected expression, got '+t.value);
      }
      while(true){
        if(is('.') && 8>=min){take();left={kind:'member',object:left,name:ident()};continue}
        if(is('(') && 8>=min){
          take();const args=[];
          while(!is(')')){args.push(expr());if(!accept(',')){if(!is(')'))fail('expected comma in argument list')}}
          need(')');left={kind:'call',callee:left,args};continue;
        }
        const op=peek().value,priority=OPS[op];
        if(priority===undefined||priority<min)break;
        take();left={kind:'binary',op,left,right:expr(priority+1)};
      }
      return left;
    });
  }
  const parseActions=()=>{
    need('{');const actions=[];
    while(!is('}')){
      if(is('<eof>'))fail('unterminated action');
      if(accept('return'))actions.push({kind:'return',value:expr()});
      else if(peek().kind==='id' && peek(1).value==='='){
        const name=ident();need('=');actions.push({kind:'assign',name,value:expr()});
      }else actions.push({kind:'effect',value:expr()});
      accept(';');
    }
    need('}');return actions;
  };
  const components=[],functions=[],states=[],loads=[],subscriptions=[],imports=[],mounts=[],routes=[];
  let view=null,theme=null;
  function children(){
    return nest(()=>{
      const list=[];need('{');
      while(!is('}')){
        if(is('<eof>'))fail('unterminated view');
        if(peek().kind==='id' && (
          peek(1).value==='{' ||
          peek(1).value==='on' ||
          (peek(1).kind==='id'&&peek(2).value===':') ||
          peek(1).value==='('
        )){
          const tag=ident();
          if(is('(')){// Component expression in child position.
            at--;list.push({kind:'child',value:expr()});accept(';');continue;
          }
          const attrs=[],events=[];
          while(!is('{')){
            if(accept('on')){need('.');const type=ident();if(!EVENTS.has(type))fail('unsupported event '+type);if(events.some(e=>e[0]===type))fail('duplicate event '+type);events.push([type,parseActions()]);continue}
            const attr=take(),name=attr.value;
            if(attr.kind!=='id'||(!ATTRS.has(name)&&!/^aria_[a-z_]+$/.test(name)&&!/^data_[a-z_]+$/.test(name)))fail('unsupported attribute '+name);
            need(':');attrs.push([name,expr()]);
          }
          list.push({kind:'element',tag,attrs,events,children:children()});accept(';');continue;
        }
        list.push({kind:'child',value:expr()});accept(';');
      }
      need('}');return list;
    });
  }
  while(!is('<eof>')){
    if(accept('module')){ident();while(accept('.'))ident();accept(';');continue}
    if(accept('import')){
      if(accept('js')){
        const spec=take();if(spec.kind!=='str')fail('import js requires a quoted module specifier');
        const value=spec.value;
        const bare=/^[a-zA-Z0-9@][a-zA-Z0-9@._/-]*$/.test(value)&&!value.includes('..');
        const local=/^(?:\.\/|\.\.\/)[a-zA-Z0-9._/-]+$/.test(value)&&!value.includes('//');
        if(!bare&&!local)fail('unsafe JavaScript module specifier');
        need('as');imports.push({kind:'js',specifier:value,alias:ident()});
      }else{
        let specifier,alias;
        if(tokens[at].kind==='str'){
          specifier=take().value;
          if(!/^noqeri\/[A-Za-z_][A-Za-z_0-9]*$/.test(specifier))
            fail('package imports use a quoted module name such as import "noqeri/ui" as ui');
          alias=accept('as')?ident():specifier.split('/')[1];
        }else{
          // Read old unquoted source without making it the recommended style.
          const namespace=ident();need('/');const pkg=ident();
          specifier=namespace+'/'+pkg;alias=accept('as')?ident():pkg;
        }
        imports.push({kind:'package',specifier,alias});
      }
    }else if(accept('let')||accept('const')||accept('persist')){
      const declaration=tokens[at-1].value,mutable=declaration!=='const',name=ident();need('=');
      states.push({name,mutable,persistent:declaration==='persist',value:expr()});
    }else if(accept('load')){
      const name=ident();need('from');const url=take();
      if(url.kind!=='str'||!/^(?:\/(?!\/)|\.(?:\/|\.\/))/.test(url.value))fail('load endpoint must be a same-origin path');
      loads.push({name,url:url.value});
    }else if(accept('subscribe')){
      const name=ident();need('from');const url=take();
      if(url.kind!=='str'||!/^\/(?!\/)[A-Za-z0-9/_-]{1,256}$/.test(url.value)||
         url.value.includes('..')||url.value.includes('//'))fail('WebSocket subscription requires fixed same-origin path');
      subscriptions.push({name,url:url.value});
    }else if(accept('function')){
      const name=ident();need('(');const params=[];
      while(!is(')')){
        const param=ident();
        if(accept(':')){const t=ident();if(!['string','int','bool','float'].includes(t))fail('unsupported function parameter type '+t)}
        params.push(param);if(!accept(',')){if(!is(')'))fail('expected comma in function arguments')}
      }
      need(')');functions.push({name,params,body:parseActions()});
    }else if(accept('component')){
      const name=ident();need('(');const params=[];
      while(!is(')')){const param=ident();if(accept(':')){const t=ident();if(!['string','int','bool','float'].includes(t))fail('unsupported component parameter type '+t)}
        params.push(param);if(!accept(',')){if(!is(')'))fail('expected comma in component arguments')}}
      need(')');components.push({name,params,children:children()});
    }else if(accept('route')){
      const path=take();
      if(path.kind!=='str'||!/^\/(?:[a-zA-Z0-9_-]+(?:\/[a-zA-Z0-9_-]+)*)?$/.test(path.value))fail('route path must be a static absolute path');
      if(routes.some(r=>r.path===path.value))fail('duplicate route '+path.value);
      routes.push({path:path.value,children:children()});
    }else if(accept('theme')){
      if(theme)fail('only one theme allowed');
      theme=Object.create(null);need('{');
      while(!is('}')){
        const key=ident();
        if(!THEME_KEYS.has(key)||Object.hasOwn(theme,key))fail('unknown or duplicate theme token '+key);
        need(':');const value=expr();
        if(value.kind!=='literal')fail('theme token must be a literal');
        theme[key]=value.value;
        accept(',');accept(';');
      }
      need('}');
    }else if(accept('view')){
      if(view)fail('only one root view allowed');view=children();
    }else if(accept('on')){
      need('.');need('mount');mounts.push(...parseActions());
    }else fail('unsupported top-level declaration '+peek().value);
    accept(';');
  }
  if(!view&&!routes.length&&!components.length&&!functions.length)fail('view, route, component or function declaration required');
  const symbols=new Set();
  for(const item of [...imports,...states,...loads,...subscriptions,...components,...functions]){
    const name=item.alias??item.name;
    if(symbols.has(name)||BAD.has(name)||!isIdent(name)||name==='input'||name==='form')fail('duplicate or forbidden binding '+name);
    symbols.add(name);
  }
  if(routes.length&&symbols.has('navigate'))fail('navigate is reserved in routed views');
  return {imports,states,loads,subscriptions,components,functions,view,mounts,routes,theme};
}
export function compileView(source,{runtimeImport='./__noqeri/runtime.mjs'}={}) {
  const ast=parseView(source);
  if(typeof runtimeImport!=='string'||!/^\.{1,2}\//.test(runtimeImport)||runtimeImport.includes('\\')||runtimeImport.includes('\n'))throw Error('Invalid runtime module import');
  const bindings=new Set([...ast.imports.map(x=>x.alias),...ast.states.map(x=>x.name),...ast.loads.map(x=>x.name),...ast.subscriptions.map(x=>x.name),...ast.components.map(x=>x.name),...ast.functions.map(x=>x.name),...(ast.routes.length?['navigate']:[])]);
  const aliases=new Set(ast.imports.map(x=>x.alias));
  const resource=new Set([...ast.loads,...ast.subscriptions].map(x=>x.name));
  const mutable=new Set(ast.states.filter(x=>x.mutable).map(x=>x.name));
  function jsExpr(e,local=new Set()){
    if(e.kind==='literal')return JSON.stringify(e.value);
    if(e.kind==='name'){if(!bindings.has(e.name)&&!local.has(e.name))throw Error('Noqeri view: undeclared name '+e.name);return e.name}
    if(e.kind==='unary')return '('+e.op+jsExpr(e.right,local)+')';
    if(e.kind==='binary'){const op=e.op==='=='?'===':e.op==='!='?'!==':e.op;return '('+jsExpr(e.left,local)+' '+op+' '+jsExpr(e.right,local)+')'}
    if(e.kind==='object')return 'Object.assign(Object.create(null),{'+e.entries.map(([k,v])=>JSON.stringify(k)+':'+jsExpr(v,local)).join(',')+'})';
    if(e.kind==='member'){
      function base(n){return n.kind==='name'?n.name:n.kind==='member'||n.kind==='call'?base(n.kind==='call'?n.callee:n.object):null}
      const root=base(e);
      if(aliases.has(root))return jsExpr(e.object,local)+'['+JSON.stringify(e.name)+']';
      if(resource.has(root)||(local.has(root)&&(root==='input'||root==='form')))return '__member('+jsExpr(e.object,local)+','+JSON.stringify(e.name)+')';
      throw Error('Noqeri view: property access requires imported library or loaded resource');
    }
    if(e.kind==='call'){
      function root(n){return n.kind==='name'?n.name:n.kind==='member'?root(n.object):n.kind==='call'?root(n.callee):null}
      if(!aliases.has(root(e.callee))&&!(e.callee.kind==='name'&&(ast.components.some(c=>c.name===e.callee.name)||ast.functions.some(f=>f.name===e.callee.name)||(ast.routes.length&&e.callee.name==='navigate'))))throw Error('Noqeri view: call requires imported library, function or component');
      return jsExpr(e.callee,local)+'('+e.args.map(a=>jsExpr(a,local)).join(',')+')';
    }
    throw Error('Noqeri view: unsupported expression');
  }
  function actions(items,local,allowReturn=false){
    return items.map(a=>{
      if(a.kind==='return'){
        if(!allowReturn)throw Error('Noqeri view: return only allowed in a function');
        return 'return '+jsExpr(a.value,local)+';';
      }
      if(a.kind==='assign'){
        if(!mutable.has(a.name))throw Error('Noqeri view: only mutable top-level state may be assigned: '+a.name);
        return a.name+' = '+jsExpr(a.value,local)+';'+
          (ast.states.some(x=>x.name===a.name&&x.persistent)?' __saveStored('+JSON.stringify(a.name)+','+a.name+');':'')+
          ' __schedule();';
      }
      if(a.value.kind!=='call')throw Error('Noqeri view: effect must call imported library or component');
      return jsExpr(a.value,local)+';';
    }).join('\n');
  }
  function nodes(items,local=new Set()){
    return '['+items.map(n=>{
      if(n.kind==='child')return jsExpr(n.value,local);
      if(!TAGS.has(n.tag))throw Error('Noqeri view: unsupported HTML tag '+n.tag);
      if(VOID.has(n.tag)&&n.children.length)throw Error('Noqeri view: void tag cannot have children: '+n.tag);
      const attrs='{'+n.attrs.map(([name,val])=>JSON.stringify(name.replace(/^(aria|data)_/,'$1-').replaceAll('_','-'))+':'+jsExpr(val,local)).join(',')+'}';
      const events='{'+n.events.map(([name,body])=>JSON.stringify(name)+':(__event)=>{const input=__inputEvent(__event);const form=__formFields(__event);'+actions(body,new Set([...local,'input','form']))+'}').join(',')+'}';
      return '__h('+JSON.stringify(n.tag)+','+attrs+','+nodes(n.children,local)+','+events+')';
    }).join(',')+']';
  }
  const lines=['// Generated from Noqeri view subset (candidate; not typed Stage-2 NIR).'];
  for(const imp of ast.imports){
    const source=imp.kind==='js'?imp.specifier:'./'+imp.specifier+'.mjs';
    lines.push('import * as '+imp.alias+' from '+JSON.stringify(source)+';');
  }
  lines.push('import { element as __h, createApp as __createApp, safeMember as __member, createRouter as __createRouter, inputEvent as __inputEvent, formFields as __formFields, readStored as __readStored, saveStored as __saveStored } from '+JSON.stringify(runtimeImport)+';');
  for(const state of ast.states)lines.push((state.mutable?'let ':'const ')+state.name+' = '+(state.persistent?'__readStored('+JSON.stringify(state.name)+','+jsExpr(state.value)+')':jsExpr(state.value))+';');
  for(const res of ast.loads)lines.push('let '+res.name+' = {loading:true,data:null,error:null};');
  for(const res of ast.subscriptions)lines.push('let '+res.name+' = {connected:false,data:null,error:null};');
  lines.push('let __schedule = ()=>{};');
  if(ast.routes.length){
    lines.push('let __router = null;');
    lines.push('export function navigate(path){return __router ? __router.navigate(path) : false;}');
  }
  for(const fn of ast.functions){
    if(new Set(fn.params).size!==fn.params.length)throw Error('Noqeri view: duplicate function parameter '+fn.name);
    lines.push('export function '+fn.name+'('+fn.params.join(',')+') { '+actions(fn.body,new Set(fn.params),true)+' }');
  }
  for(const comp of ast.components){
    const local=new Set(comp.params);
    if(new Set(comp.params).size!==comp.params.length)throw Error('Noqeri view: duplicate component parameter '+comp.name);
    lines.push('export function '+comp.name+'('+comp.params.join(',')+') { return __h("div",{},'+nodes(comp.children,local)+',{}); }');
  }
  if(ast.view||ast.routes.length){
    if(ast.routes.length){
      const page=ast.routes.reduceRight((fallback,r)=>'(__path==='+JSON.stringify(r.path)+' ? '+nodes(r.children)+' : '+fallback+')',ast.view?nodes(ast.view):'[__h("h1",{},["Page not found"],{})]');
      lines.push('export function render(path=null){const __path=path ?? (__router ? __router.path() : (typeof location!=="undefined" ? location.pathname : "/"));return __h("div",{},'+page+',{});}');
    }else lines.push('export function render(){return __h("div",{},'+nodes(ast.view)+',{});}');
    lines.push('export function mount(root){if(!root)throw Error("Missing Noqeri view root");');
    if(ast.routes.length)lines.push('  __router=__createRouter(root,()=>__schedule());');
    lines.push('  const app=__createApp(root,render,{hydrate:root.hasChildNodes()});__schedule=app.schedule;');
    for(const r of ast.subscriptions){
      lines.push('  app.socket('+JSON.stringify(r.url)+',update=>{'+r.name+'={...'+r.name+',...update,error:null};__schedule();},error=>{'+r.name+'={...'+r.name+',error:String(error)};__schedule();});');
    }
    for(const r of ast.loads){
      lines.push('  app.load('+JSON.stringify(r.url)+',value=>{'+r.name+'={loading:false,data:value,error:null};__schedule();},error=>{'+r.name+'={loading:false,data:null,error:String(error)};__schedule();});');
    }
    if(ast.mounts.length)lines.push('  '+actions(ast.mounts,new Set()));
    if(ast.routes.length)lines.push('  const __dispose=app.dispose.bind(app);app.dispose=()=>{__router?.dispose();__router=null;__schedule=()=>{};__dispose();};');
    lines.push('  return app;');
    lines.push('}');
    lines.push('if(typeof document!=="undefined"){const root=document.getElementById("app");if(root)mount(root);}');
  }
  return lines.join('\n')+'\n';
}
