import test from 'node:test'
import assert from 'node:assert/strict'
import {openDatabase,createSupabaseDatabase,createFirestoreDatabase,createSqlDatabase,DatabaseError} from '../Runtime/web-managed-databases.mjs'
const response=(body,status=200)=>({ok:status>=200&&status<300,status,text:async()=>JSON.stringify(body)})
test('Supabase uses authenticated PostgREST requests and safe identifiers',async()=>{
  const calls=[];
  const db=await openDatabase({engine:'supabase',url:'https://demo.supabase.co',apiKey:'public-key',
    accessToken:()=> 'user.jwt.token',
    fetchImpl:async(url,options)=>{calls.push({url,options});return response([{id:12,name:'A'}])}});
  assert.deepEqual(await db.list('people',{limit:2}),[{id:12,name:'A'}]);
  assert.equal((await db.get('people',12)).id,12);
  assert.equal((await db.create('people',{name:'A'})).name,'A');
  assert.equal((await db.update('people',12,{name:'B'})).id,12);
  assert.equal(await db.remove('people',12),true);
  assert.equal(calls.length,5);
  assert.equal(calls[0].options.headers.Authorization,'Bearer user.jwt.token');
  assert.equal(calls[0].options.headers.apikey,'public-key');
  assert.equal(new URL(calls[1].url).searchParams.get('id'),'eq.12');
  assert.equal(calls[3].options.method,'PATCH');
  assert.deepEqual(JSON.parse(calls[3].options.body),{name:'B'});
  await assert.rejects(db.list('people; DROP TABLE people'),/NQDB101/);
  await assert.rejects(db.get('people','1),or(id.eq.2'),/NQDB118/);
  assert.throws(()=>createSupabaseDatabase({url:'http://localhost',apiKey:'X'}),/NQDB108/);
});
test('Supabase fails closed on network/provider errors',async()=>{
  const bad=createSupabaseDatabase({url:'https://demo.supabase.co',apiKey:'key',
    fetchImpl:async()=>response({message:'secret not echoed'},403)});
  await assert.rejects(bad.list('people'),error=>error instanceof DatabaseError && error.status===403 && !error.message.includes('secret'));
});
test('Firestore encodes typed values and uses conditional writes',async()=>{
  const calls=[];
  const fetchImpl=async(url,options)=>{
    calls.push({url,options});
    const doc={name:'projects/demo/databases/(default)/documents/people/abc',
      fields:{active:{booleanValue:true},age:{integerValue:'3'},score:{doubleValue:4.5},nested:{mapValue:{fields:{name:{stringValue:'Ada'}}}}}};
    if(options.method==='GET'&&url.includes('pageSize'))return response({documents:[doc]});
    if(options.method==='DELETE')return response(null);
    return response(doc);
  };
  const db=await openDatabase({engine:'firebase',projectId:'demo',token:async()=> 'firebase-token',fetchImpl});
  assert.deepEqual(Object.assign({},(await db.list('people'))[0]),{active:true,age:3,score:4.5,nested:Object.assign(Object.create(null),{name:'Ada'}),id:'abc'});
  assert.equal((await db.get('people','abc')).age,3);
  await db.create('people',{age:2,active:false},{id:'new'});
  await db.update('people','abc',{age:5});
  await db.remove('people','abc');
  assert.match(calls[2].url,/documentId=new/);
  assert.deepEqual(JSON.parse(calls[2].options.body).fields.age,{integerValue:'2'});
  assert.match(calls[3].url,/currentDocument.exists=true/);
  assert.equal(calls[0].options.headers.Authorization,'Bearer firebase-token');
  await assert.rejects(db.create('people',{value:Number.POSITIVE_INFINITY}),/NQDB120/);
});
test('Neon/PostgreSQL operations use bind parameters and quoted checked identifiers',async()=>{
  const calls=[];
  const client={query:async(sql,params)=>{calls.push({sql,params});return {rows:[{id:1,title:'test'}]}},end:async()=>{}};
  const db=await createSqlDatabase({engine:'neon',client});
  await db.get('tasks',"1' OR 1=1 --");
  assert.match(calls[0].sql,/WHERE "id" = \$1/);
  assert.deepEqual(calls[0].params,["1' OR 1=1 --"]);
  await db.create('tasks',{title:'hello'});
  assert.match(calls[1].sql,/INSERT INTO "tasks"/);
  await db.update('tasks',1,{title:'done'});
  assert.deepEqual(calls[2].params,['done',1]);
  assert.equal(await db.remove('tasks',1),true);
  await assert.rejects(db.list('tasks; DROP TABLE'),/NQDB101/);
  await db.close();
  await assert.rejects(db.get('tasks',1),/NQDB130/);
});

test('SQLite adapter performs actual bound CRUD against node:sqlite',async()=>{
  const {DatabaseSync}=await import('node:sqlite');
  const client=new DatabaseSync(':memory:');
  try{
    client.exec('CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, done INTEGER NOT NULL)');
    const db=await createSqlDatabase({engine:'sqlite',client});
    const first=await db.create('items',{id:1,name:'quoted \' value',done:false});
    assert.equal(first.id,1);
    assert.equal(first.done,0);
    assert.equal((await db.get('items',1)).name,"quoted ' value");
    assert.equal((await db.list('items'))[0].id,1);
    assert.equal((await db.update('items',1,{done:true})).done,1);
    assert.equal(await db.remove('items',1),true);
    assert.equal(await db.remove('items',1),false);
    await db.close();
  }finally{client.close()}
});
