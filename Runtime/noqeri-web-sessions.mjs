// SPDX-License-Identifier: Apache-2.0
// Server-side HTTP session/auth primitives for Noqeri endpoint hosts (Node 22+).
// Not a drop-in security boundary: callers still enforce TLS, authorization, origin and rate limits.
import {createHmac,randomBytes,scrypt as scryptCallback,timingSafeEqual} from 'node:crypto'
import {promisify} from 'node:util'
const scrypt=promisify(scryptCallback);
const rand=()=>randomBytes(32).toString('base64url');
const constantTime=(a,b)=>{if(typeof a!=='string'||typeof b!=='string')return false;
  const x=Buffer.from(a),y=Buffer.from(b);return x.length===y.length&&timingSafeEqual(x,y)};
export function memorySessions(){
  const items=new Map();
  return {
    async get(id){return items.get(id)??null},
    async set(id,record){items.set(id,record)},
    async delete(id){items.delete(id)},
    async clearExpired(now=Date.now()){for(const [id,item] of items)if(item.expiresAt<=now)items.delete(id)}
  };
}
export async function sqliteSessions(filename){
  if(typeof filename!=='string'||!filename.trim())throw TypeError('A session database filename is required');
  const {DatabaseSync}=await import('node:sqlite');
  const db=new DatabaseSync(filename);
  db.exec('PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS noqeri_sessions (id TEXT PRIMARY KEY, value TEXT NOT NULL, expires_at INTEGER NOT NULL)');
  const get=db.prepare('SELECT value,expires_at FROM noqeri_sessions WHERE id=?');
  const put=db.prepare('INSERT INTO noqeri_sessions (id,value,expires_at) VALUES (?,?,?) ON CONFLICT(id) DO UPDATE SET value=excluded.value, expires_at=excluded.expires_at');
  const drop=db.prepare('DELETE FROM noqeri_sessions WHERE id=?');
  const gc=db.prepare('DELETE FROM noqeri_sessions WHERE expires_at<=?');
  return {
    async get(id){const item=get.get(id);return item?JSON.parse(item.value):null},
    async set(id,item){put.run(id,JSON.stringify(item),item.expiresAt)},
    async delete(id){drop.run(id)},
    async clearExpired(now=Date.now()){gc.run(now)},
    async close(){db.close()}
  };
}
export function sessionManager({secret,store=memorySessions(),cookieName='nq_session',ttlSeconds=3600,secure=true,sameSite='Lax'}={}){
  if(typeof secret!=='string'||Buffer.byteLength(secret)<32)throw Error('Noqeri sessions require a secret of at least 32 bytes');
  if(!/^[a-zA-Z_][a-zA-Z_0-9]{0,40}$/.test(cookieName))throw Error('Invalid session cookie name');
  if(!Number.isInteger(ttlSeconds)||ttlSeconds<60||ttlSeconds>7*86400)throw Error('Session lifetime must be 60 seconds to 7 days');
  if(!['Lax','Strict'].includes(sameSite))throw Error('Session SameSite must be Lax or Strict');
  if(!store||!['get','set','delete'].every(k=>typeof store[k]==='function'))throw TypeError('Invalid session store');
  const sign=id=>createHmac('sha256',secret).update(id).digest('base64url');
  const cookieOpts='; Path=/; HttpOnly; SameSite='+sameSite+(secure?'; Secure':'');
  const header=value=>cookieName+'='+value+cookieOpts;
  const expired=()=>header('')+'; Max-Age=0';
  const cookieValue=headerText=>{
    if(typeof headerText!=='string'||headerText.length>8192)return null;
    const matches=headerText.split(';').map(x=>x.trim()).filter(x=>x.startsWith(cookieName+'='));
    if(matches.length!==1)return null;
    const token=matches[0].slice(cookieName.length+1);
    const match=/^([A-Za-z0-9_-]{43})\.([A-Za-z0-9_-]{43})$/.exec(token);
    if(!match||!constantTime(sign(match[1]),match[2]))return null;
    return match[1];
  };
  const issue=async (user,{now=Date.now()}={})=>{
    if(!user||typeof user!=='object'||Array.isArray(user)||typeof user.id!=='string'||!user.id||user.id.length>256)throw TypeError('Session user requires a bounded string id');
    if(JSON.stringify(user).length>4096)throw Error('Session user exceeds 4 KiB');
    const id=rand(),csrf=rand(),expiresAt=now+ttlSeconds*1000;
    const publicUser=JSON.parse(JSON.stringify(user));
    await store.set(id,{user:publicUser,csrf,expiresAt});
    return {cookie:header(id+'.'+sign(id))+'; Max-Age='+ttlSeconds,session:{user:publicUser,csrf,expiresAt}};
  };
  const get=async (cookieHeader,{now=Date.now()}={})=>{
    const id=cookieValue(cookieHeader);if(!id)return null;
    const result=await store.get(id);if(!result)return null;
    if(!Number.isFinite(result.expiresAt)||result.expiresAt<=now){await store.delete(id);return null}
    return {user:result.user,csrf:result.csrf,expiresAt:result.expiresAt};
  };
  const destroy=async cookieHeader=>{const id=cookieValue(cookieHeader);if(id)await store.delete(id);return {cookie:expired()}};
  const rotate=async cookieHeader=>{const active=await get(cookieHeader);if(!active)return null;
    await destroy(cookieHeader);return issue(active.user)};
  const assertCsrf=(session,token)=>{
    if(!session||!constantTime(session.csrf,token))throw Error('Noqeri sessions: CSRF token is missing or invalid');
    return true;
  };
  return {issue,get,destroy,rotate,assertCsrf,clearExpired:now=>store.clearExpired?.(now)};
}
export function requireSameOrigin(origin,expectedOrigin){
  if(typeof origin!=='string'||typeof expectedOrigin!=='string')throw Error('Noqeri auth: Origin is required');
  let actual,expected;
  try{actual=new URL(origin);expected=new URL(expectedOrigin)}catch{throw Error('Noqeri auth: invalid Origin')}
  if(!['https:','http:'].includes(actual.protocol)||actual.origin!==expected.origin)
    throw Error('Noqeri auth: cross-origin state change denied');
  return true;
}
export async function hashPassword(password){
  if(typeof password!=='string'||password.length<12||password.length>1024)throw Error('Password must have 12..1024 characters');
  const salt=randomBytes(16),derived=await scrypt(password,salt,64,{N:16384,r:8,p:1,maxmem:64*1024*1024});
  return 'scrypt-v1$'+salt.toString('base64url')+'$'+derived.toString('base64url');
}
export async function verifyPassword(password,stored){
  if(typeof password!=='string'||password.length>1024||typeof stored!=='string')return false;
  const match=/^scrypt-v1\$([a-zA-Z0-9_-]{22})\$([a-zA-Z0-9_-]{86})$/.exec(stored);
  if(!match)return false;
  const salt=Buffer.from(match[1],'base64url'),expected=Buffer.from(match[2],'base64url');
  const derived=await scrypt(password,salt,expected.length,{N:16384,r:8,p:1,maxmem:64*1024*1024});
  return expected.length===derived.length&&timingSafeEqual(expected,derived);
}
export function passwordAuth({lookupUser,sessions}={}){
  if(typeof lookupUser!=='function'||!sessions||typeof sessions.issue!=='function')throw TypeError('Password auth requires lookupUser and sessions');
  return {
    async login(identifier,password){
      if(typeof identifier!=='string'||identifier.length>256||!identifier.trim())return null;
      const user=await lookupUser(identifier);
      if(!user||user.disabled||!await verifyPassword(password,user.passwordHash))return null;
      // Only public identity values go into a session. Never store password hashes there.
      return sessions.issue({id:String(user.id),...(typeof user.email==='string'?{email:user.email}:{})});
    },
    logout:cookie=>sessions.destroy(cookie)
  };
}
