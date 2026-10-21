import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
function require(ok, message) { if (!ok) throw new Error(message) }
const cli = resolve('Runtime/stage1-cli.mjs')
const root = await mkdtemp(join(tmpdir(), 'noqeri-stage1-cli-'))
try {
  const doctor = spawnSync(process.execPath, [cli, 'doctor', '--json'], { cwd: root, encoding: 'utf8' })
  require(doctor.status === 0, `doctor failed without compiler module: ${doctor.stderr}`)
  const report = JSON.parse(doctor.stdout)
  require(report.environment.compilerModuleExists === false, 'doctor failed to report missing compiler module')
  require(report.environment.nodeSupported === true, 'doctor rejected supported Node runtime')
  await writeFile(join(root, 'project.nqon'), 'name = doctor-test\nversion = "1.0.0"\nentry = main.noqeri\n')
  const env = spawnSync(process.execPath, [cli, 'env', '--json'], { cwd: root, encoding: 'utf8' })
  require(env.status === 0, `env failed: ${env.stderr}`)
  const snapshot = JSON.parse(env.stdout)
  require(snapshot.projectManifestExists === true, 'env did not discover project.nqon')
  require(snapshot.projectManifest.endsWith('project.nqon'), 'env did not prefer the NQON project manifest')
  const strict = spawnSync(process.execPath, [cli, 'doctor', '--json', '--strict'], { cwd: root, encoding: 'utf8' })
  require(strict.status === 1, 'strict doctor should fail when compiler module is absent')
  const help = spawnSync(process.execPath, [cli, '--help'], { cwd: root, encoding: 'utf8' })
  require(help.status === 0 && help.stdout.includes('doctor'), 'help path failed')
  console.log('Noqeri stage-1 CLI host tests passed')
} finally { await rm(root, { recursive: true, force: true }) }
