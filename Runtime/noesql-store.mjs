// Experimental NoeSQL .nqd reference store: checksum + atomic replacement.
// NOT a WAL database; no crash/power-loss durability, multi-process isolation,
// recovery or cross-platform fsync guarantees are claimed.
import { open, readFile, rename, unlink, lstat } from 'node:fs/promises'
import { dirname, extname, resolve } from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { execute, emptyDatabase, runNamed } from './noesql-reference.mjs'
const MAX_FILE = 16 * 1024 * 1024
function pathCheck(path) { if (extname(path)!=='.nqd') throw new Error('NoeSQL reference storage must use .nqd') }
async function regularOrAbsent(file) {
  try { const s=await lstat(file); if(!s.isFile()||s.isSymbolicLink()) throw Error('refusing nonregular or symlink path') }
  catch(e) { if(e.code!=='ENOENT') throw e }
}
function checksum(payload) { return createHash('sha256').update(payload).digest('hex') }
export async function readNqd(path) {
  pathCheck(path); await regularOrAbsent(path)
  let content
  try { content=await readFile(path) } catch(e) { if(e.code==='ENOENT') return emptyDatabase();throw e }
  if(content.length>MAX_FILE) throw Error('NoeSQL reference storage file is too large')
  let file
  try { file=JSON.parse(content.toString('utf8')) } catch { throw Error('invalid NoeSQL storage JSON') }
  if(file?.magic!=='NOESQL-NQD-EXPERIMENTAL' || file.version!==1 || typeof file.payload!=='string' || file.checksum!==checksum(file.payload)) throw Error('NoeSQL reference storage checksum/version mismatch')
  let state
  try { state=JSON.parse(file.payload) } catch { throw Error('invalid NoeSQL payload') }
  if(state?.format!=='noesql-reference-1') throw Error('invalid NoeSQL state')
  return state
}
async function persistNqd(path, state) {
  const payload=JSON.stringify(state)
  const content=JSON.stringify({magic:'NOESQL-NQD-EXPERIMENTAL',version:1,checksum:checksum(payload),payload})+'\n'
  if(Buffer.byteLength(content)>MAX_FILE) throw Error('NoeSQL reference storage file is too large')
  const temp=resolve(dirname(path),`.noesql-${randomUUID()}.tmp`)
  let file
  try { file=await open(temp,'wx',0o600);await file.writeFile(content);await file.sync() }
  finally { await file?.close() }
  try {
    await rename(temp,path)
    if(process.platform!=='win32') { const directory=await open(dirname(path),'r');try{await directory.sync()}finally{await directory.close()} }
  } catch(e) { try { await unlink(temp) } catch {} throw e }
}
export async function runNqd(path, source, params={}) {
  pathCheck(path)
  const lock=path+'.lock'
  let holder
  try { holder=await open(lock,'wx',0o600) } catch(e) { if(e.code==='EEXIST') throw Error('NoeSQL reference store locked; inspect stale locks manually');throw e }
  try { const before=await readNqd(path), outcome=execute(source,before,params); await persistNqd(path,outcome.database);return outcome.results }
  finally { await holder.close();await unlink(lock) }
}
export async function namedNqd(path, name, params={}) { return runNamed(await readNqd(path),name,params) }
