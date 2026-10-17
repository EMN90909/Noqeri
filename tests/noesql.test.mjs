import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {lex,parse} from '../Runtime/noesql/compiler.mjs';
import {run,runFile,load,emptyState} from '../Runtime/noesql/engine.mjs';
const schema=`db shop
table users {
 id: int key auto
 name: text required
 email: text required unique
 age: int?
 active: bool = true
}
index users(email)
`;
test('lexer tracks lines, columns and escape literals',()=>{const t=lex('table users {\n name: text = "A\\nB"\n}');assert.equal(t.find(x=>x.value==='name').line,2);assert.equal(t.find(x=>x.type==='str').value,'A\nB')});
test('lexer rejects unterminated strings and illegal characters',()=>{assert.throws(()=>lex('users get "oops'),/NOE100/);assert.throws(()=>lex('!'),/NOE100/)});
test('recursive descent schemas and precedence AST',()=>{const a=parse('users where age >= 18 and active == true').items[0];assert.equal(a.predicate.op,'and');assert.equal(a.predicate.left.op,'>=')});
test('schema catalog + indexes, typed CRUD, projection, sorting and limits',()=>{const a=run(schema+`users add { name: "Amina", email:"a@b", age: 24 }
users add { name:"Jules", email:"c@d", age:19 }
users get name, age where age >= 18 sort age desc take 1
users where email == "a@b" set age += 1
users where email == "c@d" delete
users get name, age`);assert.equal(a.state.version,2);assert.equal(a.output[5][0].name,'Amina');assert.equal(a.output[5][0].age,24);assert.equal(a.output.at(-1)[0].age,25);assert.equal(a.state.rows.users.length,1)});
test('duplicates, missing columns, type mismatches and dangerous writes',()=>{const a=run(schema);assert.throws(()=>run('users get fullname',a.state),/NOE101/);assert.throws(()=>run('users where age > "older" get',a.state),/NOE201|NOE100/);assert.throws(()=>run('users delete',a.state),/NOE301/);assert.throws(()=>run('users where age = 4 set name = "x"',a.state),/NOE100/)});
test('required/unique constraints and transaction isolation',()=>{const a=run(schema);assert.throws(()=>run('users add { name:"a" }',a.state),/NOE201/);assert.throws(()=>run('users add { name:"a", email:"same" }\nusers add { name:"b", email:"same" }',a.state),/NOE400/);assert.equal(a.state.rows.users.length,0)});
test('schema reference validation and foreign key integrity',()=>{const defs=`table parents { id: int key }\ntable children { id: int key\n parent: ref parents required }`;let s=run(defs);assert.throws(()=>run('children add {id:1, parent:77}',s.state),/NOE400/);s=run('parents add {id:3}\nchildren add {id:1,parent:3}',s.state);assert.throws(()=>run('parents where id == 3 delete',s.state),/NOE400/)});
test('named-query validation uses typed parameters',()=>{let s=run(schema+'query adults(minAge: int = 18) { users get name where age >= $minAge }');assert.equal(Object.keys(s.state.queries).length,1);assert.throws(()=>run('query invalid(minAge: text) { users where age >= $minAge }',s.state),/NOE201/)});
test('atomic rollback on duplicate inserts leaves file intact',()=>{const dir=mkdtempSync(join(tmpdir(),'noe-'));const f=join(dir,'shop.nqd');try{runFile(f,schema);const before=readFileSync(f,'utf8');assert.throws(()=>runFile(f,'users add {name:"a",email:"x"}\nusers add {name:"b",email:"x"}'),/NOE400/);assert.equal(readFileSync(f,'utf8'),before);assert.equal(load(f).rows.users.length,0)}finally{rmSync(dir,{recursive:true,force:true})}});
test('persistent snapshot checksum rejects corruption',()=>{const dir=mkdtempSync(join(tmpdir(),'noe-'));const f=join(dir,'shop.nqd');try{runFile(f,schema);const b=readFileSync(f,'utf8');writeFileSync(f,b.replace('shop','oops'));assert.throws(()=>load(f),/NOE600/)}finally{rmSync(dir,{recursive:true,force:true})}});
test('query parameters bind separately; no string interpolation',()=>{const s=run(schema);const v=run('users add {name:$name, email:$email}',s.state,{name:'Amina',email:'a@example'});assert.equal(v.state.rows.users[0].name,'Amina');assert.throws(()=>run('users get where name == $missing',s.state),/NOE101|NOE300/)});
test('rejects reserved JS-property identifiers',()=>{assert.throws(()=>parse('table __proto__ { id:int key }'),/NOE300/)});
test('rejects out-of-order and duplicate query clauses',()=>{assert.throws(()=>parse('users sort name where active'),/NOE100/);assert.throws(()=>parse('users where active where active'),/NOE100/)});
test('rejects unsupported relationship traversal rather than returning wrong rows',()=>{const s=run(schema);assert.throws(()=>run('users get buyer.name',s.state),/NOE101/)});
test('rejects invalid database extension',()=>{assert.throws(()=>runFile('/tmp/bad.sqlite',schema),/NOE600/)});
test('guard expression nesting',()=>{assert.throws(()=>parse('users where '+'('.repeat(70)+'true'+')'.repeat(70)),/NOE700/)});
