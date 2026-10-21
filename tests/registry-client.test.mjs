import {test} from 'node:test'
import {strict as assert} from 'node:assert'
import {createHash} from 'node:crypto'
import {gzipSync} from 'node:zlib'
import {decodePackageArchive,assertRegistryUrl} from '../Runtime/registry-client.mjs'
function archive(names){
 const chunks=[]
 for(const name of names) {
  const data=Buffer.from(name==='package.noqeri'?'package { entry: "main.noqeri" }':'module main')
  const h=Buffer.alloc(512);h.write(name,0,100)
  const oct=(n,l)=>n.toString(8).padStart(l-1,'0')+'\0'
  h.write(oct(0o644,8),100);h.write(oct(0,8),108);h.write(oct(0,8),116)
  h.write(oct(data.length,12),124);h.write(oct(0,12),136);h.fill(32,148,156)
  h[156]=48;h.write('ustar\0',257);h.write('00',263)
  let sum=0;for(const n of h)sum+=n;h.write(oct(sum,8),148)
  chunks.push(h,data,Buffer.alloc((512-data.length%512)%512))
 }
 chunks.push(Buffer.alloc(1024))
 const bytes=gzipSync(Buffer.concat(chunks),{mtime:0})
 return {bytes,integrity:'sha256:'+createHash('sha256').update(bytes).digest('hex')}
}
test('verified archives extract only regular relative paths',()=>{
 const {bytes,integrity}=archive(['main.noqeri','package.noqeri'])
 assert.deepEqual(decodePackageArchive(bytes,integrity).map(f=>f.name),['main.noqeri','package.noqeri'])
 assert.throws(()=>decodePackageArchive(bytes,'sha256:'+'f'.repeat(64)),/integrity/)
})
test('reject traversal, collisions and missing manifest',()=>{
 for(const names of [['../oops','package.noqeri'],['main.noqeri','main.noqeri','package.noqeri'],['main.noqeri']]) {
  const {bytes,integrity}=archive(names)
  assert.throws(()=>decodePackageArchive(bytes,integrity),/unsafe|duplicate|manifest/)
 }
})
test('remote registry must use HTTPS and be an origin',()=>{
 assert.equal(assertRegistryUrl('https://example.org'),'https://example.org')
 assert.equal(assertRegistryUrl('http://127.0.0.1:8080'),'http://127.0.0.1:8080')
 assert.throws(()=>assertRegistryUrl('http://example.org'),/HTTPS/)
 assert.throws(()=>assertRegistryUrl('https://example.org/sneaky'),/origin/)
})
