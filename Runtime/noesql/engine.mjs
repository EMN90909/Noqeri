import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, openSync, fsyncSync, closeSync, renameSync, mkdirSync, rmdirSync, unlinkSync, existsSync, statSync, lstatSync } from 'node:fs';
import { dirname } from 'node:path';
import { NoeError, parse } from './compiler.mjs';
const fault=(code,message,token)=>{throw new NoeError(code,message,token)};
const clone=s=>structuredClone(s);
const same=(a,b)=>a===b||(a===null&&b===null);
const builtinTypes=new Set(['int','real','bool','text','decimal','datetime','ref']);
const signature=record=>createHash('sha256').update(JSON.stringify(record)).digest('hex');
const binaryTypes=new Set(['int','real','decimal']);
const compare=(a,b,op,token)=>{
  if(a===null||b===null)return null;
  if(['<','<=','>','>='].includes(op)&&typeof a!==typeof b)fault('NOE201','Incompatible values in comparison',token);
  if(['+','-','*','/','%'].includes(op)&&!(typeof a==='number'&&typeof b==='number'))fault('NOE201','Arithmetic requires numbers',token);
  switch(op){case '==':return a===b;case '!=':return a!==b;case '<':return a<b;case '<=':return a<=b;case '>':return a>b;case '>=':return a>=b;
    case '+':return a+b;case '-':return a-b;case '*':return a*b;case '/':if(b===0)fault('NOE201','Division by zero',token);return a/b;case '%':if(b===0)fault('NOE201','Division by zero',token);return a%b;
    case 'and':return a===false||b===false?false:a===null||b===null?null:Boolean(a&&b);
    case 'or':return a===true||b===true?true:a===null||b===null?null:Boolean(a||b);
  }fault('NOE200','Unsupported operator '+op,token);
};
function evalExpr(e,row={},params={}) {
  if(!e)return null;
  switch(e.kind){
    case 'literal':return e.value;
    case 'path':if(e.parts.length!==1)fault('NOE200','Relationship traversal is not yet implemented',e.token);return row[e.parts[0]]??null;
    case 'param':if(!Object.hasOwn(params,e.name))fault('NOE300','Unbound parameter '+e.name,e.token);return params[e.name];
    case 'unary':{const n=evalExpr(e.right,row,params);if(n===null)return null;if(e.op==='not')return !n;if(e.op==='-'){if(typeof n!=='number')fault('NOE201','Unary - expects number',e.token);return -n}break}
    case 'binary':{const a=evalExpr(e.left,row,params);if(e.op==='and'&&a===false)return false;if(e.op==='or'&&a===true)return true;return compare(a,evalExpr(e.right,row,params),e.op,e.token)}
    case 'call':if(e.name==='now'&&e.args.length===0)return new Date().toISOString();if(e.name==='count'&&e.args.length===0)return 1;fault('NOE200','Unsupported function '+e.name,e.token);
  }fault('NOE200','Invalid expression',e.token);
}
function inferType(e,table,params={}){
  if(!e)return'bool';
  if(e.kind==='literal')return e.value===null?'null':typeof e.value==='number'?(Number.isInteger(e.value)?'int':'real'):typeof e.value==='boolean'?'bool':'text';
  if(e.kind==='param'){const p=params[e.name];if(!p)fault('NOE101','Unknown query parameter '+e.name,e.token);return p.type}
  if(e.kind==='path'){if(e.parts.length!==1)fault('NOE101','Relationship path requires join support',e.token);const field=table?.fields.find(f=>f.name===e.parts[0]);if(!field)fault('NOE101','Unknown field '+e.parts.join('.'),e.token);return field.type==='ref'?'int':field.type}
  if(e.kind==='call'){if(e.name==='now'&&e.args.length===0)return'datetime';if(e.name==='count'&&e.args.length===0)return'int';fault('NOE201','Unknown function '+e.name,e.token)}
  if(e.kind==='unary'){const rhs=inferType(e.right,table,params);if(e.op==='not'&&rhs==='bool')return'bool';if(e.op==='-'&&binaryTypes.has(rhs))return rhs;fault('NOE201','Bad unary operand type '+rhs,e.token)}
  if(e.kind==='binary'){
    const a=inferType(e.left,table,params),b=inferType(e.right,table,params);
    if(['and','or'].includes(e.op)){if(a!=='bool'||b!=='bool')fault('NOE201','Logical operands must be bool',e.token);return'bool'}
    if(['==','!=','<','<=','>','>='].includes(e.op)){if(a!=='null'&&b!=='null'&&a!==b&&!(binaryTypes.has(a)&&binaryTypes.has(b)))fault('NOE201',`Cannot compare ${a} and ${b}`,e.token);return'bool'}
    if(!binaryTypes.has(a)||!binaryTypes.has(b))fault('NOE201',`Arithmetic requires numbers, got ${a} and ${b}`,e.token);
    return a==='real'||b==='real'||e.op==='/'?'real':a;
  }fault('NOE201','Unknown expression',e.token);
}
function validType(f,v){if(v===null)return !!f.nullable&&!f.flags.includes('required')&&!f.flags.includes('key');if(f.type==='int'||f.type==='ref')return Number.isSafeInteger(v);if(f.type==='real'||f.type==='decimal')return typeof v==='number'&&Number.isFinite(v);if(f.type==='bool')return typeof v==='boolean';if(f.type==='text')return typeof v==='string';if(f.type==='datetime')return typeof v==='string'&&!Number.isNaN(Date.parse(v));return false}
function checkFields(fields,tk){const set=new Set(),keys=fields.filter(x=>x.flags.includes('key'));if(fields.length===0||fields.length>256)fault('NOE400','Table must contain 1â256 fields',tk);if(keys.length>1)fault('NOE400','Multiple primary keys',tk);
  for(const f of fields){if(set.has(f.name))fault('NOE400','Duplicate column '+f.name,f.token);set.add(f.name);if(!builtinTypes.has(f.type))fault('NOE201','Unknown type '+f.type,f.token);if(f.flags.includes('auto')&&(!f.flags.includes('key')||f.type!=='int'))fault('NOE400','auto requires int key',f.token);if(f.flags.includes('key')&&f.nullable)fault('NOE400','Primary key cannot be nullable',f.token);if(f.scale&&(f.type!=='decimal'||!Number.isInteger(f.scale[0])||f.scale[0]<1||f.scale[0]>38||f.scale[1]<0||f.scale[1]>f.scale[0]))fault('NOE201','Invalid decimal precision',f.token);if(f.defaultExpr){const ty=inferType(f.defaultExpr,{fields:[]});if(ty!==f.type&&!(f.type==='datetime'&&ty==='datetime')&&!(f.type==='decimal'&&binaryTypes.has(ty))&&!(f.type==='real'&&ty==='int')&&ty!=='null')fault('NOE201',`Default for ${f.name} is ${ty} not ${f.type}`,f.token)}}
}
function checkRow(state,table,row,ignore=-1){for(const k of Object.keys(row))if(!table.fields.some(f=>f.name===k))fault('NOE101','Unknown field '+k,table.token);
  for(const f of table.fields){const v=Object.hasOwn(row,f.name)?row[f.name]:null;if(!validType(f,v))fault('NOE201',`Invalid value for ${f.name} (${f.type})`,f.token);if(v!==null){if(f.type==='decimal'&&f.scale&&Math.round(v*Math.pow(10,f.scale[1]))!==v*Math.pow(10,f.scale[1]))fault('NOE201','Decimal scale exceeded for '+f.name,f.token);
      if(f.type==='ref'){const foreign=state.schemas.find(t=>t.name===f.ref),pk=foreign?.fields.find(x=>x.flags.includes('key'));if(!foreign||!pk||!state.rows[foreign.name].some(r=>r[pk.name]===v))fault('NOE400','Unresolved foreign key '+f.name,f.token)}}
    if((f.flags.includes('unique')||f.flags.includes('key'))&&v!==null)for(let i=0;i<state.rows[table.name].length;i++)if(i!==ignore&&state.rows[table.name][i][f.name]===v)fault('NOE400','Unique constraint failed: '+table.name+'.'+f.name,f.token);
  }
}
function validate(state,node,params={}){
  if(node.kind==='db')return{kind:'db',name:node.name};
  if(node.kind==='table'){checkFields(node.fields,node.token);if(state.schemas.some(t=>t.name===node.name))fault('NOE400','Duplicate table '+node.name,node.token);return{kind:'createTable',table:node}}
  if(node.kind==='index'){const table=state.schemas.find(t=>t.name===node.table);if(!table)fault('NOE101','Unknown table '+node.table,node.token);if(node.columns.length>16||!node.columns.length)fault('NOE400','Index columns out of range',node.token);for(const name of node.columns)if(!table.fields.some(f=>f.name===name))fault('NOE101','Unknown index field '+name,node.token);return{kind:'index',table:node.table,columns:node.columns}}
  if(node.kind==='named'){if(state.queries[node.name])fault('NOE400','Duplicate named query '+node.name,node.token);const types={};for(const p of node.params){if(types[p.name])fault('NOE400','Duplicate parameter '+p.name,p.token);types[p.name]={type:p.type};if(!builtinTypes.has(p.type)||p.type==='ref')fault('NOE201','Invalid parameter type '+p.type,p.token)}const query=validate(state,node.action,types);if(query.kind!=='scan')fault('NOE200','Named query must be a read',node.token);return{kind:'named',name:node.name,params:node.params,query}}
  if(node.kind==='atomic'){return{kind:'atomic',items:node.items.map(n=>validate(state,n,params))}}
  const table=state.schemas.find(t=>t.name===node.table);if(!table)fault('NOE101','Unknown table '+node.table,node.token);
  if(node.kind==='insert'){const names=new Set();for(const v of node.fields){if(names.has(v.name))fault('NOE400','Duplicate inserted field '+v.name,v.token);names.add(v.name);const f=table.fields.find(f=>f.name===v.name);if(!f)fault('NOE101','Unknown field '+v.name,v.token);const ty=inferType(v.expr,table,params);if(ty!=='null'&&ty!==f.type&&!(f.type==='ref'&&ty==='int')&&!(f.type==='real'&&ty==='int')&&!(f.type==='decimal'&&binaryTypes.has(ty)))fault('NOE201',`Expected ${f.type} for ${v.name}, got ${ty}`,v.token)}return{kind:'insert',table,fields:node.fields}}
  if(node.predicate&&inferType(node.predicate,table,params)!=='bool')fault('NOE201','where must be boolean',node.token);
  if(node.kind==='update'){for(const p of node.patch){const f=table.fields.find(f=>f.name===p.name);if(!f)fault('NOE101','Unknown field '+p.name,p.token);if(f.flags.includes('key'))fault('NOE301','Updating primary key is not supported',p.token);const ty=inferType(p.expr,table,params);if(ty!=='null'&&ty!==f.type&&!(f.type==='ref'&&ty==='int')&&!(f.type==='real'&&ty==='int')&&!(f.type==='decimal'&&binaryTypes.has(ty)))fault('NOE201','Assignment type mismatch for '+p.name,p.token);if(p.op!=='='&&!binaryTypes.has(f.type))fault('NOE201','Compound assignment requires numeric type',p.token)}return{kind:'update',table,predicate:node.predicate,patch:node.patch}}
  if(node.kind==='delete')return{kind:'delete',table,predicate:node.predicate};
  if(node.kind==='get'){for(const e of node.projection??[])inferType(e,table,params);for(const o of node.order??[])inferType(o.expr,table,params);for(const [key,e] of [['skip',node.skip],['take',node.take]])if(e&&inferType(e,table,params)!=='int')fault('NOE201',key+' requires integer',e.token);return{kind:'scan',table,predicate:node.predicate,projection:node.projection,order:node.order,skip:node.skip,take:node.take,schemaVersion:state.version}}
  fault('NOE200','Unsupported operation '+node.kind,node.token);
}
function execute(state,ir,params={}){
  if(ir.kind==='db'){state.database=ir.name;return {database:ir.name}}
  if(ir.kind==='createTable'){state.schemas.push(ir.table);state.rows[ir.table.name]=[];state.version++;return {table:ir.table.name}}
  if(ir.kind==='index'){state.indexes.push({table:ir.table,columns:ir.columns});state.version++;return{index:ir.table+'.'+ir.columns.join(',')}}
  if(ir.kind==='named'){state.queries[ir.name]={params:ir.params,query:ir.query};return{query:ir.name}}
  if(ir.kind==='atomic')return ir.items.map(item=>execute(state,item,params));
  const table=ir.table,rows=state.rows[table.name];
  if(ir.kind==='insert'){
    const row={};for(const f of ir.fields)row[f.name]=evalExpr(f.expr,{},params);
    for(const f of table.fields){if(!Object.hasOwn(row,f.name)){if(f.defaultExpr)row[f.name]=evalExpr(f.defaultExpr,{},params);else if(f.flags.includes('auto'))row[f.name]=Math.max(0,...rows.map(r=>r[f.name]??0))+1;else row[f.name]=null}}
    checkRow(state,table,row);rows.push(row);return{inserted:1,row};
  }
  if(ir.kind==='scan'){
    let entries=rows.filter(r=>!ir.predicate||evalExpr(ir.predicate,r,params)===true).map(r=>clone(r));
    if(ir.order){entries.sort((a,b)=>{for(const o of ir.order){const x=evalExpr(o.expr,a,params),y=evalExpr(o.expr,b,params);const s=x===y?0:x===null?-1:y===null?1:x<y?-1:1;if(s)return o.dir==='desc'?-s:s}return 0})}
    const skip=ir.skip?evalExpr(ir.skip,{},params):0,take=ir.take?evalExpr(ir.take,{},params):entries.length;
    if(!Number.isSafeInteger(skip)||skip<0||!Number.isSafeInteger(take)||take<0||take>100000)fault('NOE700','Invalid skip/take limit');entries=entries.slice(skip,skip+take);
    if(ir.projection?.length){entries=entries.map(r=>Object.fromEntries(ir.projection.map((p,i)=>[p.kind==='path'?p.parts.join('.'):'expr_'+i,evalExpr(p,r,params)])))}
    return entries;
  }
  if(ir.kind==='update'){let changed=0;for(let i=0;i<rows.length;i++)if(evalExpr(ir.predicate,rows[i],params)===true){const row={...rows[i]};for(const p of ir.patch){const value=evalExpr(p.expr,rows[i],params);row[p.name]=p.op==='='?value:compare(row[p.name],value,p.op.slice(0,1),p.token)}checkRow(state,table,row,i);rows[i]=row;changed++}return{updated:changed}}
  if(ir.kind==='delete'){let deleted=0;for(let i=rows.length-1;i>=0;i--)if(evalExpr(ir.predicate,rows[i],params)===true){for(const t of state.schemas)for(const f of t.fields)if(f.type==='ref'&&f.ref===table.name&&state.rows[t.name].some(r=>r[f.name]===rows[i][table.fields.find(x=>x.flags.includes('key'))?.name]))fault('NOE400','Cannot delete referenced row');rows.splice(i,1);deleted++}return{deleted}}
  fault('NOE700','Unsupported query IR '+ir.kind);
}
export function emptyState(){return{format:'NOED1',version:0,database:'',schemas:[],rows:{},indexes:[],queries:{}}}
export function run(source,original=emptyState(),params={}){
  const ast=parse(source),state=clone(original),output=[];
  const paramTypes=Object.fromEntries(Object.entries(params).map(([name,v])=>[name,{type:typeof v==='string'?'text':typeof v==='boolean'?'bool':typeof v==='number'?(Number.isInteger(v)?'int':'real'):'null'}]));
  for(const item of ast.items){const ir=validate(state,item,paramTypes);output.push(execute(state,ir,params))}
  return{state,output,ast};
}
export function load(path){if(!existsSync(path))return emptyState();const st=lstatSync(path);if(!st.isFile()||st.size>67108864)fault('NOE600','Unsafe or oversized database file');const document=JSON.parse(readFileSync(path,'utf8'));if(document?.format!=='NOED1'||!document.state||document.digest!==signature(document.state))fault('NOE600','Invalid NoeSQL file or checksum mismatch');return document.state}
export function save(path,state){const parent=dirname(path);mkdirSync(parent,{recursive:true});const lock=path+'.writer.lock';try{mkdirSync(lock)}catch{fault('NOE500','Database writer lock is held')};const temp=path+'.tmp';let fd;try{fd=openSync(temp,'wx',0o600);const bytes=JSON.stringify({format:'NOED1',state,digest:signature(state)});if(Buffer.byteLength(bytes)>67108864)fault('NOE700','Database exceeds 64 MiB');writeFileSync(fd,bytes,'utf8');fsyncSync(fd);closeSync(fd);fd=undefined;renameSync(temp,path);try{const dfd=openSync(parent,'r');fsyncSync(dfd);closeSync(dfd)}catch{/* Windows may not allow syncing directories */}}finally{if(fd!==undefined)closeSync(fd);try{unlinkSync(temp)}catch{}try{rmdirSync(lock)}catch{}}}
export function runFile(path,source,params={}){if(!path.endsWith('.nqd'))fault('NOE600','NoeSQL database file must end in .nqd');const parent=dirname(path);mkdirSync(parent,{recursive:true});const lock=path+'.writer.lock';try{mkdirSync(lock)}catch{fault('NOE500','Database writer lock is held')};try{const result=run(source,load(path),params);const tmp=path+'.tmp';let fd;try{fd=openSync(tmp,'wx',0o600);writeFileSync(fd,JSON.stringify({format:'NOED1',state:result.state,digest:signature(result.state)}));fsyncSync(fd);closeSync(fd);fd=undefined;renameSync(tmp,path);try{const dirfd=openSync(parent,'r');fsyncSync(dirfd);closeSync(dirfd)}catch{/* directory fsync unsupported on some hosts */}}finally{if(fd!==undefined)closeSync(fd);try{unlinkSync(tmp)}catch{}}return result}finally{try{rmdirSync(lock)}catch{}}}
