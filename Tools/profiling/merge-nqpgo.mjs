#!/usr/bin/env node
// Deterministic, fail-closed merger for interpreter-generated NQPGO1 profiles.
// Native machine-code PGO instrumentation and training remain separate work.
import {readFile,writeFile,rename,mkdir} from 'node:fs/promises';
import {dirname,resolve} from 'node:path';
const MAX_BYTES=4*1024*1024, MAX_ROWS=300000, MAX=18446744073709551615n;
export function parseProfile(text){
  if(typeof text!=='string'||Buffer.byteLength(text)>MAX_BYTES)throw Error('NQPGO1 profile exceeds 4 MiB');
  const lines=text.trimEnd().split(/\r?\n/);
  const header=/^NQPGO1 ([a-f0-9]{16})$/.exec(lines.shift()||'');
  if(!header)throw Error('Expected exact NQPGO1 fingerprint header');
  if(lines.length>MAX_ROWS)throw Error('NQPGO1 counter limit exceeded');
  const seen=new Set(),rows=new Map();
  for(const line of lines){
    if(!line)continue;
    const match=/^([A-Za-z_@][A-Za-z_0-9@.]*) (0|[1-9][0-9]*) ([1-9][0-9]*)$/.exec(line);
    if(!match)throw Error('Malformed NQPGO1 counter');
    const [,fn,pcRaw,hitsRaw]=match;
    const pc=BigInt(pcRaw),hits=BigInt(hitsRaw);
    if(pc>4294967295n||hits>MAX)throw Error('NQPGO1 counter overflow');
    const key=fn+' '+pc.toString();
    if(seen.has(key))throw Error('Duplicate NQPGO1 counter '+key);
    seen.add(key);rows.set(key,hits);
  }
  return {fingerprint:header[1],rows};
}
export function mergeProfiles(texts){
  if(!Array.isArray(texts)||texts.length<1||texts.length>64)throw Error('Provide 1 to 64 NQPGO1 profiles');
  let fingerprint=null;
  const merged=new Map();
  for(const text of texts){
    const profile=parseProfile(text);
    if(fingerprint!==null&&profile.fingerprint!==fingerprint)throw Error('NQPGO1 fingerprint mismatch');
    fingerprint=profile.fingerprint;
    for(const [key,hits] of profile.rows)merged.set(key,
      (merged.get(key)||0n)+hits > MAX ? MAX : (merged.get(key)||0n)+hits);
    if(merged.size>MAX_ROWS)throw Error('Merged NQPGO1 counter limit exceeded');
  }
  const rows=[...merged.entries()].sort((a,b)=>{
    const [an,ai]=a[0].split(' '),[bn,bi]=b[0].split(' ');
    if(an!==bn)return an<bn?-1:1;
    const x=BigInt(ai),y=BigInt(bi);return x<y?-1:x>y?1:0;
  });
  return 'NQPGO1 '+fingerprint+'\n'+rows.map(([k,v])=>k+' '+v).join('\n')+(rows.length?'\n':'');
}
if(process.argv[1]&&resolve(process.argv[1])===new URL(import.meta.url).pathname){
  const args=process.argv.slice(2);
  if(args.length<3||args[0]!=='--output'){
    console.error('Usage: node Tools/profiling/merge-nqpgo.mjs --output output.nqpgo profile1.nqpgo [profile2.nqpgo ...]');
    process.exit(2);
  }
  try{
    const [destination,...inputs]=args.slice(1);
    if(inputs.length>64)throw Error('Too many profile inputs');
    const contents=await Promise.all(inputs.map(path=>readFile(path,'utf8')));
    const result=mergeProfiles(contents);
    const out=resolve(destination);await mkdir(dirname(out),{recursive:true});
    const temp=out+'.tmp-'+process.pid;
    await writeFile(temp,result,{flag:'wx',mode:0o600});
    await rename(temp,out);
    console.log('Merged '+inputs.length+' compatible NQPGO1 profiles to '+out);
  }catch(error){console.error('Noqeri profile merge: '+error.message);process.exitCode=1}
}
