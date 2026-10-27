// SPDX-License-Identifier: Apache-2.0
// Server-side optional databases for Noqeri web backends. Never bundle this module into a browser.
const IDENT=/^[A-Za-z_][A-Za-z0-9_]{0,62}$/;
const DOC=/^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$/;
const own=(object,key)=>Object.hasOwn(Object(object),key);
export class DatabaseError extends Error {
  constructor(code,message,status=0){super(code+': '+message);this.name='DatabaseError';this.code=code;this.status=status}
}
function fail(code,message){throw new DatabaseError(code,message)}
function ident(value){if(typeof value!=='string'||!IDENT.test(value)||['__proto__','constructor','prototype'].includes(value))fail('NQDB101','Expected a safe database identifier');return value}
function documentId(value){if(typeof value!=='string'||!DOC.test(value)||value==='.'||value==='..')fail('NQDB102','Document IDs must contain only letters, digits, _, - or .');return value}
function record(value){
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.getPrototypeOf(value)!==Object.prototype)fail('NQDB103','Expected a plain record');
  const entries=Object.entries(value);
  if(!entries.length||entries.length>64)fail('NQDB104','Expected 1 to 64 fields');
  for(const [key,v] of entries){ident(key);if(v===undefined)fail('NQDB105','Undefined values are not supported')}
  if(JSON.stringify(value).length>128*1024)fail('NQDB106','Record exceeds 128 KiB');
  return entries;
}
function limitValue(value){if(!Number.isInteger(value)||value<1||value>100)fail('NQDB107','limit must be between 1 and 100');return value}
function urlBase(value){
  let u;try{u=new URL(value)}catch{fail('NQDB108','Invalid HTTPS base URL')}
  if(u.protocol!=='https:'||u.username||u.password||u.search||u.hash||u.pathname!=='/'||!u.hostname)fail('NQDB108','Use an HTTPS base origin without credentials or path');
  return u.origin;
}
function requestClient({fetchImpl=globalThis.fetch,timeoutMs=10000}={}){
  if(typeof fetchImpl!=='function')fail('NQDB109','fetch is required');
  if(!Number.isInteger(timeoutMs)||timeoutMs<100||timeoutMs>60000)fail('NQDB110','timeoutMs must be 100..60000');
  return async (url,{method='GET',headers={},body}={})=>{
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),timeoutMs);
    try{
      const res=await fetchImpl(url,{method,headers,body:body===undefined?undefined:JSON.stringify(body),redirect:'error',signal:controller.signal});
      const source=await res.text();
      if(source.length>1024*1024)throw new DatabaseError('NQDB111','Provider response exceeds 1 MiB',res.status);
      let data=null;try{data=source?JSON.parse(source):null}catch{throw new DatabaseError('NQDB112','Provider returned non-JSON data',res.status)}
      if(!res.ok)throw new DatabaseError('NQDB113','Provider returned HTTP '+res.status,res.status);
      return data;
    }catch(error){
      if(error instanceof DatabaseError)throw error;
      throw new DatabaseError('NQDB114',controller.signal.aborted?'Database request timed out':'Database network request failed');
    }finally{clearTimeout(timer)}
  };
}
function authToken(source){
  if(typeof source==='function')return source;
  if(typeof source==='string')return ()=>source;
  fail('NQDB115','A token or per-request token callback is required');
}
async function tokenValue(source){
  const token=await source();
  if(typeof token!=='string'||!token||/[\r\n]/.test(token)||token.length>8192)fail('NQDB116','Invalid provider token');
  return token;
}
export function createSupabaseDatabase({url,apiKey,accessToken,fetchImpl,timeoutMs}={}){
  const base=urlBase(url),request=requestClient({fetchImpl,timeoutMs});
  if(typeof apiKey!=='string'||!apiKey||/[\r\n]/.test(apiKey))fail('NQDB117','Supabase apiKey is required');
  const source=accessToken?authToken(accessToken):null;
  const endpoint=(table,params)=>{const u=new URL(base+'/rest/v1/'+ident(table));for(const [k,v] of params)u.searchParams.append(k,String(v));return u.toString()};
  async function send(table,method,params=[],data){
    const headers={apikey:apiKey,Accept:'application/json'};
    const jwt=source?await tokenValue(source):(apiKey.split('.').length===3?apiKey:null);
    if(jwt)headers.Authorization='Bearer '+jwt;
    if(data!==undefined)headers['Content-Type']='application/json';
    if(method!=='GET')headers.Prefer='return=representation';
    return request(endpoint(table,params),{method,headers,body:data});
  }
  function where(key,id){ident(key);if(typeof id!=='string'&&typeof id!=='number')fail('NQDB118','Record key must be text or number');const s=String(id);
    if(!s||s.length>256||/[(),\r\n]/.test(s))fail('NQDB118','Unsafe record key');return [[key,'eq.'+s]]}
  return {
    engine:'supabase',
    list:(table,{limit=50}={})=>send(table,'GET',[['select','*'],['limit',limitValue(limit)]]),
    async get(table,id,{key='id'}={}){const rows=await send(table,'GET',[['select','*'],...where(key,id),['limit','1']]);return rows?.[0]??null},
    async create(table,data){record(data);const rows=await send(table,'POST',[],data);return rows?.[0]??null},
    async update(table,id,data,{key='id'}={}){record(data);const rows=await send(table,'PATCH',where(key,id),data);return rows?.[0]??null},
    async remove(table,id,{key='id'}={}){const rows=await send(table,'DELETE',where(key,id));return Array.isArray(rows)&&rows.length>0},
    async close(){}
  };
}
function fireValue(v,depth=0){
  if(depth>12)fail('NQDB119','Firestore value nesting exceeds 12');
  if(v===null)return {nullValue:null};
  if(typeof v==='string')return {stringValue:v};
  if(typeof v==='boolean')return {booleanValue:v};
  if(typeof v==='number'){
    if(!Number.isFinite(v))fail('NQDB120','Firestore numbers must be finite');
    return Number.isSafeInteger(v)?{integerValue:String(v)}:{doubleValue:v};
  }
  if(Array.isArray(v)){if(v.length>256)fail('NQDB121','Firestore array exceeds 256 elements');return {arrayValue:{values:v.map(x=>fireValue(x,depth+1))}}}
  if(v&&typeof v==='object'&&Object.getPrototypeOf(v)===Object.prototype){
    const entries=Object.entries(v);if(entries.length>64)fail('NQDB122','Firestore map exceeds 64 fields');
    const fields=Object.create(null);for(const [k,x] of entries){ident(k);fields[k]=fireValue(x,depth+1)}
    return {mapValue:{fields}};
  }
  fail('NQDB123','Unsupported Firestore value type');
}
function unfire(v,depth=0){
  if(depth>12)fail('NQDB124','Firestore response too deeply nested');
  if(own(v,'nullValue'))return null;
  if(own(v,'stringValue'))return v.stringValue;
  if(own(v,'booleanValue'))return v.booleanValue;
  if(own(v,'integerValue')){const n=Number(v.integerValue);if(!Number.isSafeInteger(n))fail('NQDB125','Firestore integer exceeds JavaScript safe range');return n}
  if(own(v,'doubleValue'))return Number(v.doubleValue);
  if(own(v,'timestampValue'))return v.timestampValue;
  if(own(v,'arrayValue'))return (v.arrayValue.values||[]).map(x=>unfire(x,depth+1));
  if(own(v,'mapValue')){const out=Object.create(null);for(const [k,x] of Object.entries(v.mapValue.fields||{})){ident(k);out[k]=unfire(x,depth+1)}return out}
  fail('NQDB126','Unsupported Firestore response value');
}
function fireDoc(doc){const data=Object.create(null);for(const [key,v] of Object.entries(doc.fields||{})){ident(key);data[key]=unfire(v)}
  data.id=doc.name?.split('/').at(-1)??null;return data}
export function createFirestoreDatabase({projectId,database='(default)',token,fetchImpl,timeoutMs}={}){
  documentId(projectId);
  if(database!=='(default)')documentId(database);
  const source=authToken(token),request=requestClient({fetchImpl,timeoutMs});
  const base='https://firestore.googleapis.com/v1/projects/'+projectId+'/databases/'+encodeURIComponent(database)+'/documents';
  async function send(method,path,body){
    const bearer=await tokenValue(source);
    return request(base+path,{method,headers:{Authorization:'Bearer '+bearer,Accept:'application/json',...(body?{'Content-Type':'application/json'}:{})},body});
  }
  const fields=data=>{const entries=record(data),out=Object.create(null);for(const [k,v] of entries)out[k]=fireValue(v);return out};
  const collection=ident;
  const docPath=(table,id)=>'/'+collection(table)+'/'+documentId(String(id));
  return {
    engine:'firestore',
    async list(table,{limit=50}={}){const data=await send('GET','/'+collection(table)+'?pageSize='+limitValue(limit));return (data?.documents||[]).map(fireDoc)},
    async get(table,id){try{return fireDoc(await send('GET',docPath(table,id)))}catch(e){if(e.status===404)return null;throw e}},
    async create(table,data,{id}={}){
      const suffix=id===undefined?'':'?documentId='+encodeURIComponent(documentId(String(id)));
      return fireDoc(await send('POST','/'+collection(table)+suffix,{fields:fields(data)}));
    },
    async update(table,id,data){
      const f=fields(data),params=new URLSearchParams();
      for(const k of Object.keys(f))params.append('updateMask.fieldPaths',k);
      params.set('currentDocument.exists','true');
      return fireDoc(await send('PATCH',docPath(table,id)+'?'+params,{fields:f}));
    },
    async remove(table,id){try{await send('DELETE',docPath(table,id)+'?currentDocument.exists=true');return true}catch(e){if(e.status===404)return false;throw e}},
    async close(){}
  };
}
async function sqlProvider({engine,connectionString,file,client:providedClient}={}){
  if(engine==='sqlite'){
    if(providedClient)return {client:providedClient,dialect:'sqlite',owned:false};
    const {DatabaseSync}=await import('node:sqlite');
    const client=new DatabaseSync(file||':memory:');
    client.exec('PRAGMA foreign_keys = ON');
    return {client,dialect:'sqlite',owned:true};
  }
  if(providedClient)return {client:providedClient,dialect:'postgres',owned:false};
  if(typeof connectionString!=='string'||!/^postgres(?:ql)?:\/\//.test(connectionString))fail('NQDB127','Neon/Postgres requires a PostgreSQL connection string');
  let Client;try{({Client}=await import('pg'))}catch{fail('NQDB128','Install optional pg package for Neon/PostgreSQL')}
  const client=new Client({connectionString,ssl:{rejectUnauthorized:true}});
  await client.connect();return {client,dialect:'postgres',owned:true};
}
export async function createSqlDatabase(options={}){
  const {engine='postgres'}=options;
  if(!['postgres','neon','sqlite'].includes(engine))fail('NQDB129','Unsupported SQL provider');
  const {client,dialect,owned}=await sqlProvider({...options,engine:engine==='sqlite'?'sqlite':'postgres'});
  let closed=false;
  const keyName=options.primaryKey??'id';ident(keyName);
  const qt=v=>'"'+ident(v)+'"';
  const placeholder=i=>dialect==='sqlite'?'?':'$'+i;
  async function query(sql,params=[]){
    if(closed)fail('NQDB130','Database is closed');
    if(dialect==='sqlite'){
      const stmt=client.prepare(sql);
      if(/^\s*(SELECT|INSERT|UPDATE|DELETE)/i.test(sql) && (/^\s*SELECT/i.test(sql)||/\bRETURNING\b/i.test(sql)))return {rows:stmt.all(...params)};
      const result=stmt.run(...params);return {rows:[],affected:result.changes};
    }
    return client.query(sql,params);
  }
  const tableName=table=>qt(table);
  const row=(result)=>result.rows?.[0]??null;
  const tuple=data=>record(data).map(([k,v])=>[qt(k),v]);
  const api={
    engine,
    async list(table,{limit=50}={}){const result=await query('SELECT * FROM '+tableName(table)+' ORDER BY '+qt(keyName)+' LIMIT '+placeholder(1),[limitValue(limit)]);return result.rows},
    async get(table,id,{key=keyName}={}){const result=await query('SELECT * FROM '+tableName(table)+' WHERE '+qt(key)+' = '+placeholder(1)+' LIMIT 1',[id]);return row(result)},
    async create(table,data){
      const values=tuple(data);
      const sql='INSERT INTO '+tableName(table)+' ('+values.map(v=>v[0]).join(', ')+') VALUES ('+values.map((_,i)=>placeholder(i+1)).join(', ')+') RETURNING *';
      return row(await query(sql,values.map(v=>v[1])));
    },
    async update(table,id,data,{key=keyName}={}){
      const values=tuple(data);
      const sql='UPDATE '+tableName(table)+' SET '+values.map((v,i)=>v[0]+' = '+placeholder(i+1)).join(', ')+' WHERE '+qt(key)+' = '+placeholder(values.length+1)+' RETURNING *';
      return row(await query(sql,[...values.map(v=>v[1]),id]));
    },
    async remove(table,id,{key=keyName}={}){
      const result=await query('DELETE FROM '+tableName(table)+' WHERE '+qt(key)+' = '+placeholder(1)+' RETURNING '+qt(key),[id]);
      return result.rows.length>0;
    },
    async close(){if(closed)return;closed=true;if(owned){if(dialect==='sqlite')client.close();else await client.end()}}
  };
  return api;
}
export async function openDatabase(config={}){
  if(config.engine==='supabase')return createSupabaseDatabase(config);
  if(config.engine==='firebase'||config.engine==='firestore')return createFirestoreDatabase(config);
  if(['neon','postgres','sqlite'].includes(config.engine))return createSqlDatabase(config);
  fail('NQDB131','Choose supabase, firebase, neon, postgres or sqlite; NoeSQL and other native adapters use their existing runtime');
}
