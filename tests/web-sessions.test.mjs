import test from 'node:test'
import assert from 'node:assert/strict'
import {sessionManager,memorySessions,hashPassword,verifyPassword,passwordAuth,requireSameOrigin} from '../Runtime/noqeri-web-sessions.mjs'
test('sessions use signed unpredictable cookies, expirations and CSRF',async()=>{
  const store=memorySessions(),manager=sessionManager({secret:'a'.repeat(64),store,secure:true,ttlSeconds:60});
  const login=await manager.issue({id:'u1',email:'a@example.com'},{now:1000});
  assert.match(login.cookie,/HttpOnly; SameSite=Lax; Secure/);
  const cookie=login.cookie.split(';')[0];
  const session=await manager.get(cookie,{now:2000});
  assert.equal(session.user.id,'u1');
  assert.equal(manager.assertCsrf(session,login.session.csrf),true);
  assert.throws(()=>manager.assertCsrf(session,'bad'),/CSRF/);
  assert.equal(await manager.get(cookie+'; nq_session=duplicate',{now:2000}),null);
  assert.equal(await manager.get(cookie.replace(/.$/,'Z'),{now:2000}),null);
  assert.equal(await manager.get(cookie,{now:62000}),null);
});
test('sessions can rotate and revoke with a pluggable persistent store',async()=>{
  const mgr=sessionManager({secret:'b'.repeat(64),store:memorySessions(),secure:false});
  const first=await mgr.issue({id:'user42'});
  const cookie=first.cookie.split(';')[0];
  const rotated=await mgr.rotate(cookie);
  assert.ok(rotated.cookie!==first.cookie);
  assert.equal(await mgr.get(cookie),null);
  const next=rotated.cookie.split(';')[0];
  assert.equal((await mgr.get(next)).user.id,'user42');
  const logout=await mgr.destroy(next);
  assert.match(logout.cookie,/Max-Age=0/);
  assert.equal(await mgr.get(next),null);
});
test('passwords are salted, verified, and omitted from public session identities',async()=>{
  const hash=await hashPassword('very-long-test-password');
  assert.notEqual(hash,await hashPassword('very-long-test-password'));
  assert.equal(await verifyPassword('very-long-test-password',hash),true);
  assert.equal(await verifyPassword('wrong',hash),false);
  assert.equal(await verifyPassword('wrong','not-a-hash'),false);
  const sessions=sessionManager({secret:'x'.repeat(64)});
  const auth=passwordAuth({lookupUser:async()=>({id:'user',email:'u@example.com',passwordHash:hash}),sessions});
  assert.equal(await auth.login('u','wrong'),null);
  const success=await auth.login('u','very-long-test-password');
  assert.equal(success.session.user.email,'u@example.com');
  assert.equal(JSON.stringify(success).includes('scrypt-v1'),false);
  assert.equal(requireSameOrigin('https://example.test','https://example.test'),true);
  assert.throws(()=>requireSameOrigin('https://evil.test','https://example.test'),/cross-origin/);
  assert.throws(()=>sessionManager({secret:'too-short'}),/32 bytes/);
});
