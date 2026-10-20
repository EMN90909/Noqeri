// NoeSQL reference compiler: byte offsets, line/column diagnostics, AST and typed IR.
// Transitional JS host; the production compiler is intended to be implemented in .noqeri.
export class NoeError extends Error {
  constructor(code, message, token) {
    super(`${code} ${message} at ${token?.line ?? 1}:${token?.col ?? 1}`);
    this.code = code; this.line = token?.line ?? 1; this.column = token?.col ?? 1;
  }
}
const ops2 = new Set(['==','!=','<=','>=','+=','-=']);
const stop = new Set(['get','where','sort','group','take','skip','set','delete','add']);
export function lex(source) {
  if (Buffer.byteLength(source) > 1048576) throw new NoeError('NOE700','Script exceeds 1 MiB');
  const t = []; let i = 0, line = 1, col = 1;
  const put = (type, value, start, ln, cl) => t.push({type,value,start,end:i,line:ln,col:cl});
  const advance = () => {const c=source[i++]; if(c==='\n'){line++;col=1;} else col++;return c};
  while (i<source.length) {
    const c=source[i];
    if(c===' '||c==='\r'||c==='\t'){advance();continue}
    if(c==='\n'||c===';'){const at=i,ln=line,cl=col;advance();put('sep',';',at,ln,cl);continue}
    if(source.slice(i,i+2)==='//'||source.slice(i,i+2)==='--') {while(i<source.length&&source[i]!=='\n')advance();continue}
    const at=i,ln=line,cl=col;
    if(/[A-Za-z_]/.test(c)){let s='';while(i<source.length&&/[A-Za-z_0-9]/.test(source[i]))s+=advance();if(Buffer.byteLength(s)>63)throw new NoeError('NOE100','Identifier exceeds 63 bytes',{line:ln,col:cl});put('id',s,at,ln,cl);continue}
    if(/[0-9]/.test(c)){let s='';while(i<source.length&&/[0-9]/.test(source[i]))s+=advance();if(source[i]==='.'&&/[0-9]/.test(source[i+1]??'')){s+=advance();while(i<source.length&&/[0-9]/.test(source[i]))s+=advance()}put('num',s,at,ln,cl);continue}
    if(c==='"'){advance();let s='',closed=false;while(i<source.length){const v=advance();if(v==='"'){closed=true;break}if(v==='\\'){if(i>=source.length)break;const e=advance();if(!'"\\nrt'.includes(e))throw new NoeError('NOE100','Unsupported string escape',{line:ln,col:cl});s+=({n:'\n',r:'\r',t:'\t'}[e]??e)}else s+=v;}if(!closed)throw new NoeError('NOE100','Unterminated string',{line:ln,col:cl});put('str',s,at,ln,cl);continue}
    const two=source.slice(i,i+2);
    if(ops2.has(two)){advance();advance();put('sym',two,at,ln,cl);continue}
    if('{}():,?.$=+-*/%<>'.includes(c)){advance();put('sym',c,at,ln,cl);continue}
    throw new NoeError('NOE100',`Unexpected character ${JSON.stringify(c)}`,{line:ln,col:cl});
  }
  t.push({type:'eof',value:'<eof>',start:i,end:i,line,col});
  return t;
}
const prec={or:1,and:2,'==':3,'!=':3,'<':3,'<=':3,'>':3,'>=':3,'+':4,'-':4,'*':5,'/':5,'%':5};
export class Parser {
  constructor(source){this.t=lex(source);this.i=0;this.depth=0}
  get tok(){return this.t[this.i]}
  is(v){return this.tok.value===v}
  take(){return this.t[this.i++]}
  accept(v){if(this.is(v)){this.take();return true}return false}
  require(v){if(!this.accept(v))throw new NoeError('NOE100',`Expected ${v}, got ${this.tok.value}`,this.tok)}
  name(){if(['__proto__','prototype','constructor'].includes(this.tok.value))throw new NoeError('NOE300','Reserved identifier',this.tok);if(this.tok.type!=='id'||stop.has(this.tok.value))throw new NoeError('NOE100','Expected identifier',this.tok);return this.take().value}
  sep(){while(this.tok.type==='sep')this.take()}
  expectEnd(){if(this.tok.type!=='sep'&&this.tok.type!=='eof'&&!this.is('}'))throw new NoeError('NOE100','Expected end of statement',this.tok)}
  literal(){const n=this.tok;if(n.type==='str'){this.take();return {kind:'literal',value:n.value,token:n}}if(n.type==='num'){this.take();const v=Number(n.value);if(!Number.isFinite(v)||!Number.isSafeInteger(v)&&!n.value.includes('.'))throw new NoeError('NOE200','Invalid numeric literal',n);return{kind:'literal',value:v,token:n}}if(['true','false','null'].includes(n.value)){this.take();return{kind:'literal',value:n.value==='null'?null:n.value==='true',token:n}}return null}
  expression(min=0){if(this.depth++>64)throw new NoeError('NOE700','Expression depth exceeds 64',this.tok);try{let lhs;const tk=this.tok;
    if(this.accept('not')||this.accept('-'))lhs={kind:'unary',op:tk.value,right:this.expression(6),token:tk};
    else if(this.accept('(')){lhs=this.expression();this.require(')')}
    else if(this.accept('$')){lhs={kind:'param',name:this.name(),token:tk}}
    else if((lhs=this.literal())){}
    else if(this.tok.type==='id'){const name=this.name();lhs={kind:'path',parts:[name],token:tk};while(this.accept('.'))lhs.parts.push(this.name());if(this.accept('(')){const args=[];if(!this.is(')')){do{args.push(this.expression())}while(this.accept(','))}this.require(')');lhs={kind:'call',name,args,token:tk}}}
    else throw new NoeError('NOE100','Expected expression',tk);
    while(prec[this.tok.value]>=min&&prec[this.tok.value]!==undefined){const op=this.take();const rhs=this.expression(prec[op.value]+1);lhs={kind:'binary',op:op.value,left:lhs,right:rhs,token:op}}
    return lhs;
  }finally{this.depth--}}
  field(){const tk=this.tok;const name=this.name();this.require(':');let type=this.name(),ref=null,scale=null;
    if(type==='ref'){ref=this.name();type='ref'}
    else if(this.accept('(')){const a=this.take(), b=this.accept(',')?this.take():null; if(a.type!=='num'||b&&b.type!=='num')throw new NoeError('NOE200','Decimal precision/scale must be integers',tk);this.require(')');scale=[Number(a.value),b?Number(b.value):0]}
    const nullable=this.accept('?');const flags=[];while(['key','auto','unique','required'].includes(this.tok.value))flags.push(this.take().value);
    const defaultExpr=this.accept('=')?this.expression():null;this.expectEnd();return{kind:'field',name,type,ref,scale,nullable,flags,defaultExpr,token:tk};
  }
  fields(){this.require('{');const fields=[];this.sep();while(!this.is('}')){if(this.tok.type==='eof')throw new NoeError('NOE100','Unclosed table',this.tok);fields.push(this.field());this.sep()}this.require('}');return fields}
  object(){this.require('{');const fields=[];this.sep();while(!this.is('}')){const token=this.tok;const name=this.name();this.require(':');const expr=this.expression();fields.push({name,expr,token});if(!this.accept(','))this.expectEnd();this.sep()}this.require('}');return fields}
  statement(){this.sep();const tk=this.tok;if(this.accept('db'))return{kind:'db',name:this.name(),token:tk};
    if(this.accept('table')){const name=this.name(),fields=this.fields();return{kind:'table',name,fields,token:tk}}
    if(this.accept('index')){const table=this.name(),columns=[];this.require('(');do{columns.push(this.name())}while(this.accept(','));this.require(')');return{kind:'index',table,columns,token:tk}}
    if(this.accept('query')){const name=this.name(),params=[];this.require('(');if(!this.is(')')){do{const pt=this.tok,pn=this.name();this.require(':');const type=this.name();const defaultExpr=this.accept('=')?this.literal():null;params.push({name:pn,type,defaultExpr,token:pt})}while(this.accept(','))}this.require(')');this.require('{');this.sep();const action=this.statement();this.sep();this.require('}');return{kind:'named',name,params,action,token:tk}}
    if(this.accept('atomic')){this.require('{');const items=[];this.sep();while(!this.is('}')){if(this.tok.type==='eof')throw new NoeError('NOE100','Unclosed atomic block',this.tok);items.push(this.statement());this.sep()}this.require('}');return{kind:'atomic',items,token:tk}}
    const table=this.name();let operation='get', projection=null, predicate=null,order=null,take=null,skip=null,patch=null;
    if(this.accept('add'))return{kind:'insert',table,fields:this.object(),token:tk};
    const clauseSeen=new Set();let rank=0;
    while(true){this.sep();const clause=this.tok.value;const orders={get:1,where:2,sort:3,skip:4,take:5};if(!(clause in orders)&&!['set','delete'].includes(clause))break;
      if(clause==='set'){this.take();if(!predicate)throw new NoeError('NOE301','Update requires where',this.tok);patch=[];do{const n=this.name();const op=this.take();if(!['=','+=','-='].includes(op.value))throw new NoeError('NOE100','Expected assignment operator',op);patch.push({name:n,op:op.value,expr:this.expression(),token:op})}while(this.accept(','));operation='update';break}
      if(clause==='delete'){this.take();if(!predicate)throw new NoeError('NOE301','Delete requires where',this.tok);operation='delete';break}
      if(clauseSeen.has(clause)||orders[clause]<rank)throw new NoeError('NOE100',`Duplicate or out-of-order clause ${clause}`,this.tok);
      rank=orders[clause];clauseSeen.add(clause);this.take();
      if(clause==='get'){projection=[];if(this.tok.type==='id'&&!stop.has(this.tok.value)){do{projection.push(this.expression())}while(this.accept(','))}}
      if(clause==='where')predicate=this.expression();
      if(clause==='sort'){order=[];do{const expr=this.expression();let dir='asc';if(this.is('asc')||this.is('desc'))dir=this.take().value;order.push({expr,dir})}while(this.accept(','))}
      if(clause==='skip')skip=this.expression();if(clause==='take')take=this.expression();
    }
    return{kind:operation,table,projection,predicate,order,skip,take,patch,token:tk};
  }
  program(){const items=[];this.sep();while(this.tok.type!=='eof'){if(items.length>=10000)throw new NoeError('NOE700','Too many statements',this.tok);items.push(this.statement());this.sep()}return{kind:'program',items}}
}
export function parse(source){return new Parser(source).program()}
