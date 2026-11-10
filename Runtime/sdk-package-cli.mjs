#!/usr/bin/env node
// SDK package manager. The existing registry client verifies immutable package
// hashes, checks paths and size limits, and never runs downloaded package code.
import { createPackageManager } from './package-manager.mjs'

const [command, ...args] = process.argv.slice(2)
function usage() {
  console.log('Noqeri SDK package manager (requires Node.js 20+)\n'
    + '  noqeri pkg add namespace/name[@version]\n'
    + '  noqeri pkg install\n'
    + '  noqeri pkg resolve file.noqeri\n'
    + '  noqeri pkg vendor [--offline]\n'
    + '  noqeri pkg audit [--offline]\n'
    + 'Environment: NOQERI_HOME, NOQERI_OFFLINE=1, NOQERI_REGISTRY_INDEX')
}
try {
  if (!command || ['help', '--help', '-h'].includes(command)) {
    usage()
  } else if (command === 'add') {
    if (args.length !== 1 || args[0].startsWith('-')) throw new Error('add requires namespace/name[@version]')
    await createPackageManager().addPackage(args[0])
  } else if (command === 'install') {
    if (args.length) throw new Error('install accepts no arguments')
    await createPackageManager().installProject()
  } else if (command === 'resolve') {
    if (args.length !== 1) throw new Error('resolve requires a Noqeri source file')
    for (const pkg of await createPackageManager().resolvePackageImports(args[0]))
      console.log(pkg.coordinate + '@' + pkg.version + ': ' + pkg.entry + ' (' + pkg.integrity + ')')
  } else if (command === 'vendor' || command === 'audit') {
    if (args.some(a => a !== '--offline')) throw new Error(command + ' only accepts --offline')
    const offline = args.includes('--offline') || process.env.NOQERI_OFFLINE === '1'
    if (command === 'vendor') {
      const { vendorProject } = await import('./ecosystem-tools.mjs')
      const result = await vendorProject({ projectDir: process.cwd(), offline })
      console.log('Vendored ' + result.packages.length + ' package(s) to ' + result.vendorHome)
    } else {
      const { auditProject } = await import('./ecosystem-tools.mjs')
      const result = await auditProject({ projectDir: process.cwd(), offline })
      console.log(JSON.stringify(result.report, null, 2))
      process.exitCode = result.exitCode
    }
  } else {
    throw new Error('unknown package command: ' + command)
  }
} catch (error) {
  console.error('noqeri pkg: ' + error.message)
  process.exitCode = 1
}
