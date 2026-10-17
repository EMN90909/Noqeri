#!/usr/bin/env node
import {readFileSync} from 'node:fs';
import {runFile} from './engine.mjs';
import {parse} from './compiler.mjs';
const [mode,...args]=process.argv.slice(2);
function usage(){console.error('Usage: node Runtime/noesql/cli.mjs <check file.noe | run file.noe data.nqd>');process.exit(2)}
if(!['check','run'].includes(mode)||args.length!==(mode==='check'?1:2))usage();
const [file,database]=args;
if(!file.endsWith('.noe')||(database&&!database.endsWith('.nqd')))usage();
try{const text=readFileSync(file,'utf8');if(mode==='check'){const ast=parse(text);console.log(`NoeSQL syntax: ${ast.items.length} statements parsed`)}else{const result=runFile(database,text);for(const item of result.output)console.log(JSON.stringify(item))}}
catch(e){console.error(e.message);process.exit(1)}
