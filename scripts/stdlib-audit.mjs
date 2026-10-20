#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const root = path.resolve(process.cwd(), 'Lib', 'std');
const args = new Set(process.argv.slice(2));
const jsonOnly = args.has('--json');
const strict = args.has('--strict');
const targetBytes = readNumberArg('--target-bytes', 30 * 1024);
const targetLines = readNumberArg('--target-lines', 200);
const targetExports = readNumberArg('--target-exports', 10);

function readNumberArg(name, fallback) {
  const argv = process.argv.slice(2);
  const index = argv.indexOf(name);
  if (index < 0 || index + 1 >= argv.length) return fallback;
  const value = Number(argv[index + 1]);
  return Number.isFinite(value) && value >= 0 ? value : fallback;
}

function walk(directory) {
  const entries = fs.readdirSync(directory, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) files.push(...walk(absolute));
    else if (entry.isFile() && entry.name.endsWith('.noqeri')) files.push(absolute);
  }
  return files.sort();
}

function stripComments(source) {
  return source
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/.*$/gm, '');
}

function countMatches(source, pattern) {
  return [...source.matchAll(pattern)].length;
}

function nonBlankLines(source) {
  return source.split(/\r?\n/).filter((line) => line.trim().length > 0).length;
}

function classify(metrics) {
  const obviousPlaceholder = metrics.codeLines <= 12 ||
    (metrics.bytes < 1200 && metrics.exports <= 4 && metrics.records === 0);
  if (obviousPlaceholder) return 'placeholder';

  const deepApi = metrics.codeLines >= targetLines && metrics.exports >= targetExports;
  const substantialSpecialist = metrics.codeLines >= Math.floor(targetLines * 0.60) &&
    metrics.exports >= Math.max(6, Math.floor(targetExports * 0.70)) &&
    metrics.records + metrics.privateFunctions >= 3;

  if (deepApi || substantialSpecialist) return 'substantial';
  return 'developing';
}

function audit(file) {
  const source = fs.readFileSync(file, 'utf8');
  const code = stripComments(source);
  const relative = path.relative(root, file).replaceAll(path.sep, '/');
  const lines = source.split(/\r?\n/).length;
  const codeLines = nonBlankLines(code);
  const exports = countMatches(code, /^\s*export\s+function\s+[A-Za-z_][A-Za-z0-9_]*\s*</gm) +
    countMatches(code, /^\s*export\s+function\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/gm);
  const records = countMatches(code, /^\s*record\s+[A-Za-z_][A-Za-z0-9_]*/gm);
  const privateFunctions = countMatches(code, /^\s*function\s+[A-Za-z_][A-Za-z0-9_]*\s*</gm) +
    countMatches(code, /^\s*function\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/gm);
  const loops = countMatches(code, /\bwhile\b/gm);
  const branches = countMatches(code, /\bif\b/gm);
  const imports = countMatches(code, /^\s*import\s+/gm);
  const compatibility = countMatches(source, /compatib(?:ility|le)/gim);
  const bytes = Buffer.byteLength(source, 'utf8');

  const metrics = {
    module: relative,
    bytes,
    lines,
    codeLines,
    exports,
    records,
    privateFunctions,
    loops,
    branches,
    imports,
    compatibility,
    meetsRequestedByteTarget: bytes >= targetBytes,
    meetsBroadLineTarget: codeLines >= targetLines,
    meetsApiTarget: exports >= targetExports,
  };
  metrics.tier = classify(metrics);
  return metrics;
}

function aggregate(modules) {
  const byTier = { placeholder: 0, developing: 0, substantial: 0 };
  let totalBytes = 0;
  let totalLines = 0;
  let totalExports = 0;
  for (const module of modules) {
    byTier[module.tier] += 1;
    totalBytes += module.bytes;
    totalLines += module.codeLines;
    totalExports += module.exports;
  }
  return {
    moduleCount: modules.length,
    totalBytes,
    totalCodeLines: totalLines,
    totalExports,
    targetBytes,
    targetLines,
    targetExports,
    byTier,
    requestedByteTargetCount: modules.filter((module) => module.meetsRequestedByteTarget).length,
    broadLineTargetCount: modules.filter((module) => module.meetsBroadLineTarget).length,
    placeholderCount: byTier.placeholder,
  };
}

function pad(value, width, alignRight = true) {
  const text = String(value);
  return alignRight ? text.padStart(width) : text.padEnd(width);
}

function printTable(modules, summary) {
  console.log('Noqeri stdlib maturity audit');
  console.log(`modules=${summary.moduleCount}  placeholders=${summary.placeholderCount}  substantial=${summary.byTier.substantial}`);
  console.log(`requested byte target: ${(targetBytes / 1024).toFixed(0)} KiB (reported, not used as a maturity definition)`);
  console.log('');
  console.log(`${pad('module', 30, false)} ${pad('bytes', 8)} ${pad('code', 6)} ${pad('exports', 7)} ${pad('records', 7)} ${pad('tier', 12, false)}`);
  console.log('-'.repeat(78));
  for (const module of modules) {
    console.log(`${pad(module.module, 30, false)} ${pad(module.bytes, 8)} ${pad(module.codeLines, 6)} ${pad(module.exports, 7)} ${pad(module.records, 7)} ${pad(module.tier, 12, false)}`);
  }
  console.log('');
  console.log('Interpretation:');
  console.log('- placeholder: still looks like a name-only shim or tiny helper set');
  console.log('- developing: useful code exists, but broad modules still need API/test depth');
  console.log('- substantial: enough implementation surface to review behavior rather than file presence');
  console.log('- 30 KiB is shown because it is a project request, but comments/repetition never upgrade maturity');
}

if (!fs.existsSync(root)) {
  console.error(`stdlib directory not found: ${root}`);
  process.exit(2);
}

const modules = walk(root).map(audit);
modules.sort((left, right) => {
  const tierOrder = { placeholder: 0, developing: 1, substantial: 2 };
  const tierDelta = tierOrder[left.tier] - tierOrder[right.tier];
  if (tierDelta !== 0) return tierDelta;
  if (left.codeLines !== right.codeLines) return left.codeLines - right.codeLines;
  return left.module.localeCompare(right.module);
});

const summary = aggregate(modules);
const report = {
  schema: 1,
  generatedAt: new Date().toISOString(),
  policy: {
    targetBytes,
    targetLines,
    targetExports,
    note: 'Byte size is reported for transparency but does not by itself make a module mature.',
  },
  summary,
  modules,
};

if (jsonOnly) console.log(JSON.stringify(report, null, 2));
else printTable(modules, summary);

if (strict && summary.placeholderCount > 0) process.exit(1);
