import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { resolve, dirname, extname, relative, sep } from 'node:path'

// Browser ES-module emitter for Stage-2 validated Noqeri source.
// Semantic/type ownership stays in the Noqeri Stage-2 compiler. This module is
// deliberately only the portable JS code-emission host and must not be used as
// a replacement type checker.
const forbiddenNames = new Set(['eval','Function','globalThis','window','document','process','require','prototype','__proto__','constructor','new','this','await','yield','class'])
const nativeOnlyCalls = new Set(['asm','intrinsic','host','abi'])
const keywords = new Set(['function','return','if','else','while','repeat','let','const','export','extern','module','import','package','record','unsafe','throw','try','as','true','false','null'])

function lex(source) {
  const tokens=[]
  let i=0
  const re=/\s+|\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|0[xX][0-9a-fA-F]+|\d+(?:\.\d+)?|[A-Za-z_$][\w$]*|===|!==|==|!=|<=|>=|&&|\|\||->|[{}()[\],;:.+*\/%!<>=\-&]/y
  while(i<source.length) {
    re.lastIndex=i
    const match=re.exec(source)
    if(!match) throw Error(`Unsupported token at character ${i}`)
    i=re.lastIndex
    const value=match[0]
    if(/^\s/.test(value)||value.startsWith('//')||value.startsWith('/*')) continue
    tokens.push({value,start:match.index,end:i})
  }
  tokens.push({value:'<eof>',start:source.length,end:source.length})
  return tokens
}

function isIdentifier(value) { return /^[A-Za-z_$][\w$]*$/.test(value) && !keywords.has(value) }
function quote(value) { return JSON.stringify(value) }
function replaceExtension(path, extension='.js') { return path.replace(/\.nqr(?=([?#]|$))/i, extension) }
function inside(root,file){ const rel=relative(resolve(root),resolve(file)); return rel===''||(rel!=='..'&&!rel.startsWith('..'+sep)) }
function commentPath(file){ return String(file).replace(/[\r\n\u2028\u2029]/g,' ') }

class Parser {
  constructor(source,{modulePath='<memory>'}={}) {
    this.source=source
    this.tokens=lex(source)
    this.i=0
    this.modulePath=modulePath
    this.externs=new Set()
    this.functions=new Set()
    this.records=new Set()
    this.imports=[]
    this.body=[]
  }
  peek(offset=0){ return this.tokens[Math.min(this.i+offset,this.tokens.length-1)].value }
  token(){ return this.tokens[this.i] }
  get(){ return this.tokens[this.i++].value }
  accept(value){ if(this.peek()===value){this.i++;return true} return false }
  need(value){ if(!this.accept(value)) throw Error(`${this.modulePath}: expected ${value}; got ${this.peek()} at token ${this.i}`) }
  identifier(){ const value=this.get(); if(!isIdentifier(value)||forbiddenNames.has(value)) throw Error(`${this.modulePath}: invalid identifier ${value}`); return value }
  stringLiteral(){ const token=this.get(); if(!/^['"]/.test(token)) throw Error(`${this.modulePath}: expected string literal; got ${token}`); try{return JSON.parse(token[0]==='"'?token:'"'+token.slice(1,-1).replace(/"/g,'\\"')+'"')}catch{return token.slice(1,-1)} }

  parseType() {
    const start=this.i
    let pointer=false, slice=false, arrayLength=null, named=null
    while(this.accept('*')) { pointer=true; this.accept('volatile') }
    if(this.accept('[')) {
      if(this.accept(']')) { slice=true; const inner=this.parseType(); return {kind:'slice',inner,raw:this.raw(start)} }
      const inner=this.parseType(); this.need(';'); const lengthToken=this.get(); if(!/^\d+$/.test(lengthToken)) throw Error(`${this.modulePath}: fixed array length must be an integer literal`); this.need(']'); return {kind:'array',inner,length:Number(lengthToken),raw:this.raw(start)}
    }
    named=this.identifier()
    if(this.accept('<')) {
      let depth=1
      while(depth>0){ const v=this.get(); if(v==='<eof>')throw Error(`${this.modulePath}: unterminated generic type`); if(v==='<')depth++; else if(v==='>')depth-- }
    }
    return {kind:pointer?'pointer':'named',name:named,raw:this.raw(start),slice,arrayLength}
  }
  raw(start){ return this.tokens.slice(start,this.i).map(t=>t.value).join('') }
  defaultForType(type) {
    if(!type) return 'undefined'
    if(type.kind==='array') {
      const inner=this.defaultForType(type.inner)
      const factory=(type.inner?.kind==='named'&&!['int','float','bool','string','u8','u16','u32','u64','i8','i16','i32','i64','usize','isize','f32','f64'].includes(type.inner.name)) ? '({})' : inner
      return `Array.from({length:${type.length}},()=>${factory})`
    }
    if(type.kind==='slice') return '[]'
    if(type.kind==='pointer') return 'null'
    if(type.kind==='named') {
      if(type.name==='bool') return 'false'
      if(type.name==='string') return "''"
      if(type.name==='void') return 'undefined'
      if(['float','f32','f64','int','u8','u16','u32','u64','i8','i16','i32','i64','usize','isize'].includes(type.name)) return '0'
      return '{}'
    }
    return 'undefined'
  }

  parseProgram() {
    while(this.peek()!=='<eof>') this.parseDeclaration(true)
    return {imports:this.imports,body:this.body.join(''),externs:[...this.externs]}
  }
  parseDeclaration(topLevel=false) {
    if(this.accept('module')) { this.identifier(); while(this.accept('.')) this.identifier(); this.accept(';'); return '' }
    if(this.accept('import')) {
      const isPackage=this.accept('package')
      const spec=this.stringLiteral(); this.accept(';')
      this.imports.push({package:isPackage,spec})
      return ''
    }
    if(this.accept('record')) {
      const name=this.identifier(); this.records.add(name); this.need('{')
      while(!this.accept('}')) { this.identifier(); this.need(':'); this.parseType(); if(!this.accept(','))this.accept(';') }
      this.accept(';')
      return ''
    }
    const exported=this.accept('export')
    const external=this.accept('extern')
    if(this.accept('function')) {
      const out=this.parseFunction({exported,external})
      if(topLevel) this.body.push(out)
      return out
    }
    if(exported||external) throw Error(`${this.modulePath}: export/extern must modify a function`)
    const out=this.parseStatement()
    if(topLevel) this.body.push(out)
    return out
  }
  parseFunction({exported=false,external=false}={}) {
    const name=this.identifier()
    if(this.functions.has(name)||this.externs.has(name)) throw Error(`${this.modulePath}: duplicate function ${name}`)
    if(this.accept('<')) { let depth=1; while(depth){ const v=this.get(); if(v==='<eof>')throw Error(`${this.modulePath}: unterminated generic parameter list`); if(v==='<')depth++; else if(v==='>')depth-- } }
    this.need('(')
    const params=[]
    if(this.peek()!==')') do { const param=this.identifier(); if(this.accept(':')) this.parseType(); params.push(param) } while(this.accept(','))
    this.need(')')
    if(this.accept(':')) this.parseType()
    if(external) {
      if(exported) throw Error(`${this.modulePath}: cannot export external function ${name}`)
      this.externs.add(name); this.accept(';'); return ''
    }
    this.functions.add(name)
    const body=this.parseBlock()
    return `${exported?'export ':''}function ${name}(${params.join(', ')}) ${body}\n`
  }
  parseBlock() {
    this.need('{')
    let out='{\n'
    while(this.peek()!=='}') {
      if(this.peek()==='<eof>') throw Error(`${this.modulePath}: unclosed block`)
      out+=this.parseDeclaration(false)
    }
    this.need('}')
    return out+'}\n'
  }
  parseCondition() {
    if(this.accept('(')){ const expression=this.parseExpression(); this.need(')'); return expression }
    return this.parseExpression(0,new Set(['{']))
  }
  parseStatement() {
    if(this.peek()==='{') return this.parseBlock()
    if(this.accept('unsafe')) return this.parseBlock()
    if(this.accept('return')) {
      if(this.accept(';')) return 'return;\n'
      if(this.peek()==='}') return 'return;\n'
      const value=this.parseExpression(); this.accept(';'); return `return ${value};\n`
    }
    if(this.accept('throw')) { const value=this.parseExpression(); this.accept(';'); return `return -Math.abs(Number(${value})||1);\n` }
    if(this.accept('let')||this.accept('const')) {
      const kind=this.tokens[this.i-1].value
      const name=this.identifier()
      let type=null
      if(this.accept(':')) type=this.parseType()
      let value=this.defaultForType(type)
      if(this.accept('=')) value=this.parseExpression()
      this.accept(';')
      return `${kind} ${name} = ${value};\n`
    }
    if(this.accept('if')) {
      const condition=this.parseCondition(), thenBody=this.parseBlockOrStatement()
      let out=`if (${condition}) ${thenBody}`
      if(this.accept('else')) out+=`else ${this.peek()==='if'?this.parseStatement():this.parseBlockOrStatement()}`
      return out
    }
    if(this.accept('while')) { const condition=this.parseCondition(); return `while (${condition}) ${this.parseBlockOrStatement()}` }
    if(this.accept('repeat')) {
      const count=this.parseExpression(0,new Set(['{']))
      const body=this.parseBlock()
      const id=`__nqRepeat${this.i}`
      return `for (let ${id}=0, __nqLimit${this.i}=Math.max(0,Number(${count})|0); ${id}<__nqLimit${this.i}; ${id}++) ${body}`
    }
    const expression=this.parseExpression(); this.accept(';'); return `${expression};\n`
  }
  parseBlockOrStatement() { return this.peek()==='{'?this.parseBlock():`{\n${this.parseStatement()}}\n` }

  parseExpression(minPrec=0,stops=new Set([';',')',']',',','}'])) {
    let left=this.parseUnary(stops)
    const precedence={'=':1,'||':2,'&&':3,'==':4,'!=':4,'<':5,'<=':5,'>':5,'>=':5,'+':6,'-':6,'*':7,'/':7,'%':7}
    while(true) {
      const op=this.peek()
      if(stops.has(op)||op==='<eof>') break
      const prec=precedence[op]
      if(!prec||prec<minPrec) break
      this.get()
      const right=this.parseExpression(op==='='?prec:prec+1,stops)
      if(op==='=') left=this.assignment(left,right)
      else left=`(${left} ${op==='=='?'===':op==='!='?'!==':op} ${right})`
    }
    return left
  }
  parseUnary(stops) {
    if(this.accept('!')) return `(!${this.parseUnary(stops)})`
    if(this.accept('-')) return `(-${this.parseUnary(stops)})`
    if(this.accept('+')) return `(+${this.parseUnary(stops)})`
    if(this.accept('try')) return this.parseUnary(stops)
    if(this.accept('&')) {
      const target=this.parsePostfix(stops,true)
      return this.reference(target)
    }
    if(this.accept('*')) return `__nqDeref(${this.parseUnary(stops)})`
    let value=this.parsePrimary(stops)
    value=this.parsePostfixFrom(value,stops)
    while(this.accept('as')) { const type=this.parseType(); value=`__nqCast(${value},${quote(type.raw||type.name||'')})` }
    return value
  }
  parsePostfix(stops,forReference=false) { const value=this.parsePrimary(stops); return this.parsePostfixFrom(value,stops,forReference) }
  parsePostfixFrom(initial,stops,forReference=false) {
    let value=initial
    while(true) {
      if(this.accept('(')) {
        const args=[]
        if(this.peek()!==')') do { args.push(this.parseExpression(0,new Set([',',')']))) } while(this.accept(','))
        this.need(')')
        const bare=/^[A-Za-z_$][\w$]*$/.test(value)?value:null
        if(bare && (nativeOnlyCalls.has(bare)||bare.startsWith('atomic'))) throw Error(`${this.modulePath}: native-only capability ${bare} is unavailable in web modules`)
        if(bare==='print') value=`console.log(${args.join(', ')})`
        else if(bare==='len') value=`__nqLen(${args.join(', ')})`
        else if(bare==='slice') value=`__nqSlice(${args.join(', ')})`
        else value=`${value}(${args.join(', ')})`
        continue
      }
      if(this.accept('[')) { const index=this.parseExpression(0,new Set([']'])); this.need(']'); value=`__nqIndex(${value},${index})`; continue }
      if(this.accept('.')) { const member=this.identifier(); value=`${value}.${member}`; continue }
      break
    }
    return value
  }
  parsePrimary(stops) {
    const value=this.peek()
    if(value==='<eof>'||stops.has(value)) throw Error(`${this.modulePath}: expected expression; got ${value}`)
    if(this.accept('(')){ const inner=this.parseExpression(0,new Set([')'])); this.need(')'); return `(${inner})` }
    if(this.accept('[')) { const items=[]; if(this.peek()!==']') do { items.push(this.parseExpression(0,new Set([',',']']))) } while(this.accept(',')); this.need(']'); return `[${items.join(', ')}]` }
    if(/^"|^'/.test(value)) { this.get(); if(value[0]==='"') return value; return quote(value.slice(1,-1)) }
    if(/^0[xX]/.test(value)||/^\d/.test(value)||['true','false','null'].includes(value)){ this.get(); return value }
    if(isIdentifier(value)) { const name=this.identifier(); if(forbiddenNames.has(name)) throw Error(`${this.modulePath}: unsafe browser identifier ${name}`); return name }
    throw Error(`${this.modulePath}: unsupported expression token ${value}`)
  }
  assignment(left,right) {
    const index=/^__nqIndex\((.*),(.*)\)$/.exec(left)
    if(index) return `__nqSetIndex(${index[1]},${index[2]},${right})`
    if(/^__nqDeref\(/.test(left)) return `__nqSetDeref(${left.slice(10,-1)},${right})`
    return `(${left} = ${right})`
  }
  reference(target) {
    const index=/^__nqIndex\((.*),(.*)\)$/.exec(target)
    if(index) return `__nqRef(()=>__nqIndex(${index[1]},${index[2]}),v=>__nqSetIndex(${index[1]},${index[2]},v))`
    if(/^[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*$/.test(target)) {
      const dot=target.lastIndexOf('.')
      if(dot<0) return `__nqRef(()=>${target},v=>{${target}=v})`
      const base=target.slice(0,dot),key=target.slice(dot+1)
      return `__nqRef(()=>${base}.${key},v=>{${base}.${key}=v})`
    }
    throw Error(`${this.modulePath}: address-of requires an assignable value`)
  }
}

const runtimePrelude=`// Generated Noqeri portable web module.\n'use strict';\nconst __nqHost=()=>globalThis.NoqeriHost;\nconst __nqNeedHost=(name)=>{const h=__nqHost();if(!h||typeof h[name]!=='function')throw Error(\`Missing Noqeri host capability: \${name}\`);return h[name].bind(h)};\nconst __nqNonNull=(v,w='value')=>{if(v===null||v===undefined)throw Error(\`Noqeri null check failed for \${w}\`);return v};\nconst __nqLen=(v)=>Number(__nqNonNull(v,'len').length??0);\nconst __nqCheckedIndex=(v,i)=>{v=__nqNonNull(v,'index base');const n=__nqLen(v);i=Number(i);if(!Number.isInteger(i)||i<0||i>=n)throw RangeError(\`Noqeri index out of bounds: index=\${i}, length=\${n}\`);return i};\nconst __nqIndex=(v,i)=>v[__nqCheckedIndex(v,i)];\nconst __nqSetIndex=(v,i,n)=>{v[__nqCheckedIndex(v,i)]=n;return n};\nconst __nqSlice=(base,length=undefined)=>{base=__nqNonNull(base,'slice');const n=length===undefined?__nqLen(base):Number(length);if(!Number.isInteger(n)||n<0||n>__nqLen(base))throw RangeError('invalid Noqeri slice length');return new Proxy({},{get(_t,p){if(p==='length')return n;if(p===Symbol.iterator)return function*(){for(let i=0;i<n;i++)yield base[i]};if(/^\\d+$/.test(String(p))){const i=Number(p);if(i>=n)return undefined;return base[i]}return base[p]},set(_t,p,v){if(/^\\d+$/.test(String(p))){const i=Number(p);if(i<0||i>=n)throw RangeError('Noqeri slice write out of bounds');base[i]=v;return true}base[p]=v;return true}})};\nconst __nqRef=(get,set)=>new Proxy({__nqGet:get,__nqSet:set},{get(t,p){if(p==='__nqGet'||p==='__nqSet')return t[p];const v=get();return __nqNonNull(v,'pointer')[p]},set(_t,p,v){const target=__nqNonNull(get(),'pointer');target[p]=v;return true}});\nconst __nqDeref=(p)=>__nqNonNull(p,'pointer').__nqGet();\nconst __nqSetDeref=(p,v)=>{__nqNonNull(p,'pointer').__nqSet(v);return v};\nconst __nqCast=(value,type)=>{switch(type){case'u8':return Number(value)&255;case'i8':return (Number(value)<<24)>>24;case'u16':return Number(value)&65535;case'i16':return (Number(value)<<16)>>16;case'u32':return Number(value)>>>0;case'i32':return Number(value)|0;case'bool':return Boolean(value);case'string':return String(value);default:return Number.isNaN(Number(value))?value:Number(value)}};\n`

function hostWrappers(externs) { return externs.map(name=>`const ${name}=(...args)=>__nqNeedHost(${quote(name)})(...args);\n`).join('') }

export function compileWeb(source,{modulePath='<memory>'}={}) {
  const parsed=new Parser(source,{modulePath}).parseProgram()
  if(parsed.imports.length) throw Error(`${modulePath}: imports require compileWebGraph/compileWebFile so they can be resolved safely`)
  return runtimePrelude+hostWrappers(parsed.externs)+parsed.body
}

async function loadGraph(entry,{readSource=async path=>readFile(path,'utf8'),resolvePackage=null,root=null}={}) {
  const seen=new Set(), ordered=[]
  async function visit(file) {
    const absolute=resolve(file)
    if(seen.has(absolute)) return
    seen.add(absolute)
    const source=await readSource(absolute)
    const parsed=new Parser(source,{modulePath:absolute}).parseProgram()
    for(const imp of parsed.imports) {
      let child
      if(imp.package) {
        if(!resolvePackage) throw Error(`${absolute}: package import ${imp.spec} has no resolver`)
        child=await resolvePackage(imp.spec,absolute)
      } else {
        child=resolve(dirname(absolute),imp.spec)
        if(root && !inside(root,child)) throw Error(`${absolute}: local import escapes web project: ${imp.spec}`)
      }
      if(extname(child).toLowerCase()!=='.nqr') throw Error(`${absolute}: Noqeri imports must resolve to .nqr source: ${imp.spec}`)
      await visit(child)
    }
    ordered.push({file:absolute,parsed})
  }
  await visit(entry)
  return ordered
}

export async function compileWebGraph(entry,options={}) {
  const graph=await loadGraph(entry,options)
  const externs=new Set(), functionOwners=new Map(), chunks=[]
  for(const {file,parsed} of graph) {
    for(const name of parsed.externs) externs.add(name)
    for(const match of parsed.body.matchAll(/(?:^|\n)(?:export\s+)?function\s+([A-Za-z_$][\w$]*)/g)) {
      const name=match[1], prior=functionOwners.get(name)
      if(prior&&prior!==file) throw Error(`duplicate function ${name} across ${prior} and ${file}`)
      functionOwners.set(name,file)
    }
    chunks.push(`\n// ${commentPath(file)}\n${parsed.body}`)
  }
  return runtimePrelude+hostWrappers([...externs])+chunks.join('')
}

export async function compileWebFile(input,output,options={}) {
  const js=await compileWebGraph(input,options)
  await mkdir(dirname(resolve(output)),{recursive:true})
  await writeFile(output,js)
  return {input:resolve(input),output:resolve(output),bytes:Buffer.byteLength(js)}
}

export { replaceExtension }
