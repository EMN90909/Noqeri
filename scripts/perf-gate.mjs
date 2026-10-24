#!/usr/bin/env node
import { readFileSync } from 'node:fs'

if (process.argv.length < 4) {
  console.error('usage: node scripts/perf-gate.mjs <baseline.json> <candidate.json> [--release] [--require=id,id]')
  process.exit(2)
}
const baseline = JSON.parse(readFileSync(process.argv[2], 'utf8'))
const candidate = JSON.parse(readFileSync(process.argv[3], 'utf8'))
const release = process.argv.includes('--release')
const required = (process.argv.find(x => x.startsWith('--require=')) || '')
  .replace(/^--require=/, '').split(',').filter(Boolean)
const base = new Map((baseline.results || []).map(r => [r.id, r]))
const next = new Map((candidate.results || []).map(r => [r.id, r]))
const failures = [], checked = [], policyErrors = []

function positive(value) {
  return typeof value === 'number' && Number.isFinite(value) && value > 0
}
function validateSamples(before, after, id) {
  for (const [name, row] of [['baseline', before], ['candidate', after]]) {
    if (!row || row.evidence !== 'measured-local') {
      policyErrors.push(id + ': ' + name + ' has no measured-local evidence')
      continue
    }
    if (!Array.isArray(row.raw_samples_ms) || row.raw_samples_ms.length < 10 ||
        row.raw_samples_ms.some(n => !positive(n))) {
      policyErrors.push(id + ': ' + name + ' requires at least 10 valid raw samples')
    }
    if (!positive(row.median_ms) || !positive(row.p95_ms))
      policyErrors.push(id + ': ' + name + ' missing valid median/p95')
  }
  if (before?.evidence === 'measured-local' && after?.evidence === 'measured-local') {
    if (before.execution_model !== after.execution_model ||
        before.self_hosted_compiler !== after.self_hosted_compiler ||
        before.validated_stdout !== after.validated_stdout)
      policyErrors.push(id + ': execution model, self-host status or output differs')
    if (id.startsWith('noqeri:') && id !== 'noqeri:compilation' &&
        (before.execution_model !== 'native-aot' ||
         after.execution_model !== 'native-aot' ||
         !before.artifact_sha256 || !after.artifact_sha256))
      policyErrors.push(id + ': runtime comparison lacks native AOT hash evidence')
  }
}
if (release) {
  if (baseline.schema !== 3 || candidate.schema !== 3)
    policyErrors.push('release evidence requires schema 3 benchmark reports')
  if (!required.length)
    policyErrors.push('release mode requires --require=id,id to specify coverage')
  const keys = ['platform', 'arch', 'os_release', 'cpus', 'logical_cpus',
    'total_memory_bytes', 'node', 'safety_mode', 'checked_overflow']
  for (const field of keys) {
    const before = baseline.environment?.[field], after = candidate.environment?.[field]
    if (before == null || after == null || JSON.stringify(before) !== JSON.stringify(after))
      policyErrors.push('release environment missing or mismatched: ' + field)
  }
  if (!baseline.source_revision || !candidate.source_revision)
    policyErrors.push('release evidence must identify both source revisions')
  for (const id of required) validateSamples(base.get(id), next.get(id), id)
  if ((baseline.failures || []).length || (candidate.failures || []).length)
    policyErrors.push('release reports contain benchmark failures')
}
const ids = release ? required : (candidate.results || []).map(r => r.id)
for (const id of ids) {
  const row = next.get(id), before = base.get(id)
  if (!row || !before) continue
  if (release && (row.evidence !== 'measured-local' ||
                  before.evidence !== 'measured-local')) continue
  const threshold = Number(row.threshold_percent ?? before.threshold_percent ?? 8)
  const oldMedian = Number(before.median_ms), newMedian = Number(row.median_ms)
  if (!(oldMedian > 0) || !(newMedian >= 0))continue
  if (release && (!positive(threshold) ||
                  !positive(before.p95_ms) || !positive(row.p95_ms)))continue
  const medianChange = ((newMedian - oldMedian) / oldMedian) * 100
  const p95Change = release
    ? ((row.p95_ms - before.p95_ms) / before.p95_ms) * 100 : null
  const record = {
    id, oldMedian, newMedian, change_percent: medianChange,
    p95_change_percent: p95Change, threshold_percent: threshold
  }
  checked.push(record)
  if (medianChange > threshold || (release && p95Change > threshold))
    failures.push(record)
}
console.log(JSON.stringify({ release, checked, failures, policy_errors: policyErrors }, null, 2))
if (policyErrors.length)process.exit(2)
if (!checked.length) {
  console.error('perf-gate: no comparable result IDs; refusing to report a passing gate')
  process.exit(2)
}
process.exit(failures.length ? 1 : 0)
