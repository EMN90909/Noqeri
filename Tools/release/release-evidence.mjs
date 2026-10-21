#!/usr/bin/env node
import { createHash, randomUUID } from 'node:crypto'
import { mkdir, readFile, readdir, lstat, writeFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { basename, join, relative, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'

const args = process.argv.slice(2)
const artifactDir = resolve(args.find(x => !x.startsWith('--')) || 'dist/release')
const version = args.find(x => x.startsWith('--version='))?.slice(10)
const verifyMatrix = args.includes('--verify-matrix')
const sign = args.includes('--sign')
if (!version || !/^[0-9A-Za-z][0-9A-Za-z.+-]*$/.test(version)) {
  console.error('release-evidence: --version=<version> is required')
  process.exit(2)
}
if (!existsSync(artifactDir)) {
  console.error(`release-evidence: artifact directory does not exist: ${artifactDir}`)
  process.exit(2)
}

async function files(root) {
  const out = []
  async function walk(dir) {
    for (const name of (await readdir(dir)).sort()) {
      if (['checksums.txt','checksums.txt.asc','release-manifest.json','sbom.cdx.json'].includes(name) && dir === root) continue
      const path = join(dir, name)
      const info = await lstat(path)
      if (info.isSymbolicLink()) throw new Error('release inputs must not contain symlinks: '+path)
      if (info.isDirectory()) await walk(path)
      else if (info.isFile()) out.push(path)
    }
  }
  await walk(root)
  return out
}
function hash(bytes) { return createHash('sha256').update(bytes).digest('hex') }
const artifacts = []
for (const path of await files(artifactDir)) {
  const bytes = await readFile(path)
  artifacts.push({ path: relative(artifactDir, path).replaceAll('\\','/'), bytes: bytes.length, sha256: hash(bytes) })
}
if (!artifacts.length) {
  console.error('release-evidence: no release artifacts found')
  process.exit(2)
}

const supportPolicy=JSON.parse(await readFile(new URL('../../release/platforms-v1.json',import.meta.url),'utf8'))
if(supportPolicy.version!==version||!Array.isArray(supportPolicy.platforms))throw new Error('release policy version mismatch')
const alternatives = {
 'windows-x86_64':[/windows[-_.](x86_64|amd64)/i,/(x86_64|amd64)[-_.]windows/i],
 'windows-arm64':[/windows[-_.](arm64|aarch64)/i,/(arm64|aarch64)[-_.]windows/i],
 'linux-x86_64':[/linux[-_.](x86_64|amd64)/i,/(x86_64|amd64)[-_.]linux/i],
 'linux-arm64':[/linux[-_.](arm64|aarch64)/i,/(arm64|aarch64)[-_.]linux/i],
 'macos-x86_64':[/(macos|darwin)[-_.](x86_64|amd64)/i,/(x86_64|amd64)[-_.](macos|darwin)/i],
 'macos-arm64':[/(macos|darwin)[-_.](arm64|aarch64)/i,/(arm64|aarch64)[-_.](macos|darwin)/i]
}
const platformMatrix=supportPolicy.platforms.map(item=>({
 id:item.id,release_required:item.release_required===true,tier:item.tier,
 present:artifacts.some(a=>(alternatives[item.id]||[]).some(re=>re.test(a.path)))
}))
if (platformMatrix.some(p=>!(p.id in alternatives)))throw new Error('unknown platform in policy')
if (verifyMatrix && platformMatrix.some(x=>x.release_required&&!x.present)) {
 console.error('release-evidence: required artifact matrix incomplete: '+platformMatrix.filter(x=>x.release_required&&!x.present).map(x=>x.id).join(', '))
 process.exit(1)
}

const checksums = artifacts.map(a => `${a.sha256}  ${a.path}`).join('\n') + '\n'
await writeFile(join(artifactDir, 'checksums.txt'), checksums)
const epoch = process.env.SOURCE_DATE_EPOCH ? Number(process.env.SOURCE_DATE_EPOCH) : null
const timestamp = Number.isFinite(epoch) ? new Date(epoch * 1000).toISOString() : new Date().toISOString()
const serialSeed = hash(Buffer.from(version + '\n' + checksums))
const serial = `urn:uuid:${serialSeed.slice(0,8)}-${serialSeed.slice(8,12)}-4${serialSeed.slice(13,16)}-a${serialSeed.slice(17,20)}-${serialSeed.slice(20,32)}`
const sbom = {
  bomFormat:'CycloneDX', specVersion:'1.5', serialNumber:serial, version:1,
  metadata:{ timestamp, component:{ type:'application', name:'noqeri-release', version } },
  components: artifacts.map(a => ({ type:'file', name:a.path, hashes:[{ alg:'SHA-256', content:a.sha256 }], properties:[{ name:'noqeri:bytes', value:String(a.bytes) }] }))
}
await writeFile(join(artifactDir, 'sbom.cdx.json'), JSON.stringify(sbom, null, 2) + '\n')
const manifest = {
  schema:1, version, generated_at:timestamp, source_revision:process.env.NOQERI_SOURCE_REVISION || null,
  source_date_epoch:process.env.SOURCE_DATE_EPOCH || null, artifacts, platform_matrix:platformMatrix,
  checksums:'checksums.txt', sbom:'sbom.cdx.json', signature: sign ? 'checksums.txt.asc' : null,
  note:'Artifact presence is not runtime qualification. Complete release requires signed manifest, trusted bootstrap, actual platform tests and NoeSQL ecosystem E2E.'
}
await writeFile(join(artifactDir, 'release-manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
if (sign) {
  const signature = join(artifactDir, 'checksums.txt.asc')
  const cmd = ['--batch','--yes','--armor','--detach-sign','--output',signature]
  if (process.env.NOQERI_RELEASE_GPG_KEY) cmd.push('--local-user', process.env.NOQERI_RELEASE_GPG_KEY)
  cmd.push(join(artifactDir, 'checksums.txt'))
  const result = spawnSync('gpg', cmd, { stdio:'inherit' })
  if (result.error || result.status !== 0) {
    console.error('release-evidence: signing failed; no unsigned artifact may be relabelled as signed')
    process.exit(1)
  }
}
console.log(JSON.stringify({ version, artifacts:artifacts.length, platform_matrix:platformMatrix, checksums:'checksums.txt', sbom:'sbom.cdx.json', signed:sign }, null, 2))
