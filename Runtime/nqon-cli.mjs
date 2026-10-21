#!/usr/bin/env node
import { readFile } from 'node:fs/promises'
import { parseNqon, stringifyNqon } from './nqon.mjs'

function usage() {
  console.log(`NQON reference tool

Usage:
  nqon check <file.nqon>
  nqon to-json <file.nqon>
  nqon from-json <file.json>

This tool is independent of the Noqeri compiler. It is a reference implementation
for the language-neutral .nqon format.`)
}

const [command = 'help', file] = process.argv.slice(2)
try {
  if (command === 'help' || command === '--help') { usage(); process.exit(0) }
  if (!file) throw new Error(`${command} needs a file`)
  const text = await readFile(file, 'utf8')
  if (command === 'check') {
    parseNqon(text)
    console.log(`${file}: NQON OK`)
  } else if (command === 'to-json') {
    console.log(JSON.stringify(parseNqon(text), null, 2))
  } else if (command === 'from-json') {
    console.log(stringifyNqon(JSON.parse(text)).trimEnd())
  } else {
    usage()
    process.exitCode = 2
  }
} catch (error) {
  console.error(`nqon: ${error.message}`)
  process.exitCode = 1
}
