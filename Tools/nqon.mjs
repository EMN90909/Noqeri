#!/usr/bin/env node
import { readFile, writeFile } from 'node:fs/promises'
import { parseNqon, stringifyNqon } from '../Runtime/nqon.mjs'

function usage() {
  console.error('usage: node Tools/nqon.mjs <check|to-json|from-json|format> <file> [output]')
}

async function main() {
  const [command, file, output] = process.argv.slice(2)
  if (!command || !file) { usage(); process.exitCode = 2; return }

  if (command === 'from-json') {
    const value = JSON.parse(await readFile(file, 'utf8'))
    const result = stringifyNqon(value)
    if (output) await writeFile(output, result)
    else process.stdout.write(result)
    return
  }

  const text = await readFile(file, 'utf8')
  const value = parseNqon(text, { source: file })
  if (command === 'check') {
    console.log('valid NQON: ' + file)
    return
  }
  if (command === 'to-json') {
    const result = JSON.stringify(value, null, 2) + '\n'
    if (output) await writeFile(output, result)
    else process.stdout.write(result)
    return
  }
  if (command === 'format') {
    await writeFile(output || file, stringifyNqon(value))
    return
  }

  usage()
  process.exitCode = 2
}

main().catch(error => {
  console.error('nqon: ' + error.message)
  process.exitCode = 1
})
