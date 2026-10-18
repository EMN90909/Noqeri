#!/usr/bin/env node
// Local release gate: prove generated Stage-2 ESM exposes its required ABI.
import { stat } from 'node:fs/promises'
import { pathToFileURL } from 'node:url'
import { resolve } from 'node:path'

const file=resolve(process.env.NOQERI_STAGE2_MODULE || 'build/noqeri-stage2.mjs')
const required=['stage1VersionMajor','stage1VersionMinor','stage1VersionPatch','stage2SyntaxCheck','stage2SemanticCheckMerged','stage2CompileReady','stage2CompileCheck']
try {
 const metadata=await stat(file)
 if(!metadata.isFile()||metadata.size===0)throw Error('missing or empty Stage-2 module')
 const compiler=await import(pathToFileURL(file).href)
 const absent=required.filter(name=>typeof compiler[name]!=='function')
 if(absent.length)throw Error('Stage-2 compiler ABI incomplete; missing '+absent.join(', '))
 const version=[compiler.stage1VersionMajor(),compiler.stage1VersionMinor(),compiler.stage1VersionPatch()]
 if(!version.every(value=>Number.isInteger(value) && value >= 0))throw Error('Stage-2 reported an invalid version tuple')
 console.log('Stage-2 ESM ABI validated: '+required.join(', ')+' version='+version.join('.'))
} catch(e) {
 console.error('Stage-2 verification failed: '+e.message)
 console.error('The Noqeri Stage-2 host needs an executable ES module; raw binary NQO2 cannot be imported as JS.')
 process.exitCode=1
}
