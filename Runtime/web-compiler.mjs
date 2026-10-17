import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { resolve, dirname } from 'node:path'

// Restricted ES-module backend. CLI validates source with Stage-2 before invoking this.
// Unsupported constructs fail closed; no eval or dynamic Function is used.
const types = new Set(['string','bool','int','i8','i16','i32','i64','u8','u16','u32','u64','usize','isize','float','f32','f64','void'])
const forbidden = new Set(['eval','Function','globalThis','window','document','process','require','import','prototype','__proto__','constructor','new','this','await','yield','class'])
function lex(s) {
  const t=[]; let i=0
  const re=/\s+|\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\d+(?:\.\d+)?|[A-Za-z_$][\w$]*|===|!==|==|!=|<=|>=|&&|\|\||[{}(),;:.+*\/%!<>=\-]/y
  while(i<s.length) {
    re.lastIndex=i; const m=re.exec(s)
    if(!m) throw Error('Unsupported token at character '+i)
    i=re.lastIndex
    if(!/^\s|^\/\//.test(m[0])&&!m[0].startsWith('/*')) t.push(m[0])
  }
  return [...t,'<eof>']
}
export function compileWeb(source) {
  const t=lex(source); let i=0, output='// Noqeri restricted browser ES module\n'
  const externs=new Set(),functions=new Set()
  const peek=()=>t[i], get=()=>t[i++], accept=x=>peek()===x?(i++,true):false
  function need(x){if(!accept(x))throw Error('Expected '+x+'; got '+peek()+' at token '+i)}
  function ident(){const x=get();if(!/^[A-Za-z_$][\w$]*$/.test(x)||forbidden.has(x)||['function','return','if','else','while','let','const','export','extern','module'].includes(x))throw Error('Invalid identifier '+x);return x}
  function type(){const x=get();if(!types.has(x))throw Error('Unsupported type '+x)}
  function expr(stops) {
    let depth=0,result=''
    while(true) {
      const v=peek()
      if(depth===0&&stops.includes(v))break
      if(v==='<eof>')throw Error('Unterminated expression')
      if(v==='(')depth++
      if(v===')'){if(!depth)throw Error('Unexpected )');depth--}
      if(forbidden.has(v)||v==='asm'||v==='intrinsic')throw Error('Unsafe or unsupported browser expression: '+v)
      if(['{','}','[',']',';',':','.','=','=>'].includes(v))throw Error('Unsupported expression token '+v)
      if(v==='as'){get();type();continue}
      const x=get()
      result+=(x==='=='?'===':x==='!='?'!==':x)+' '
    }
    if(!result.trim()||depth!==0)throw Error('Invalid expression')
    return result.trim()
  }
  function block(){need('{');let b='{\n';while(peek()!=='}'){if(peek()==='<eof>')throw Error('Unclosed function block');b+=stmt()}need('}');return b+'}\n'}
  function stmt() {
    const k=get()
    if(k==='return'){if(accept(';'))return 'return;\n';const v=expr([';','}']);accept(';');return 'return '+v+';\n'}
    if(k==='let'||k==='const'){const x=ident();need(':');type();need('=');const v=expr([';','}']);accept(';');return k+' '+x+' = '+v+';\n'}
    if(k==='if'||k==='while'){need('(');const condition=expr([')']);need(')');const body=block();if(k==='if'&&accept('else'))return 'if ('+condition+') '+body+'else '+block();return k+' ('+condition+') '+body}
    throw Error('Unsupported statement '+k)
  }
  while(peek()!=='<eof>') {
    if(accept('module')){ident();while(accept('.'))ident();accept(';');continue}
    const exp=accept('export'),ext=accept('extern')
    need('function');const fn=ident()
    if(externs.has(fn)||functions.has(fn))throw Error('Duplicate function '+fn)
    need('(');const params=[]
    if(peek()!==')')do{const p=ident();need(':');type();params.push(p)}while(accept(','))
    need(')');if(accept(':'))type()
    if(ext){if(exp)throw Error('Cannot export external function');externs.add(fn);accept(';');continue}
    functions.add(fn);output+=(exp?'export ':'')+'function '+fn+'('+params.join(', ')+') '+block()
  }
  for(const fn of externs) {
    const escaped=JSON.stringify(fn)
    output='const '+fn+' = (...args) => { const host=globalThis.NoqeriHost; if (!host || typeof host['+escaped+'] !== "function") throw Error("Missing Noqeri host capability: '+fn+'"); return host['+escaped+'](...args) };\n'+output
  }
  return output
}
export async function compileWebFile(input,output) {
  const js=compileWeb(await readFile(input,'utf8'))
  await mkdir(dirname(resolve(output)),{recursive:true})
  await writeFile(output,js)
}
