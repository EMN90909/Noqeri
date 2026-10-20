#!/usr/bin/env node
// Reference/parity VM only. The compiler bootstrap now uses Runtime/native/nqo_vm.cpp.
// Keep this implementation for differential testing while the native host is qualified.
import { readFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const I64_MIN = -(1n << 63n)
const I64_MAX = (1n << 63n) - 1n
const U64_MAX = (1n << 64n) - 1n

class NqoError extends Error {
  constructor(message, instruction = null) {
    super(message)
    this.name = 'NqoError'
    this.instruction = instruction
  }
}
class NqoThrow extends Error {
  constructor(value) { super(`Noqeri throw: ${String(value)}`); this.name = 'NqoThrow'; this.value = value }
}
class MemoryBlock {
  constructor(bytes = 0, logicalLength = null) {
    this.buffer = new Uint8Array(Math.max(0, Number(bytes)))
    this.refs = new Map()
    this.logicalLength = logicalLength
  }
  ensure(end) {
    if (end <= this.buffer.length) return
    let size = Math.max(16, this.buffer.length || 0)
    while (size < end) size = Math.max(end, size * 2)
    const grown = new Uint8Array(size)
    grown.set(this.buffer)
    this.buffer = grown
  }
}
function ptr(block, offset = 0) { return { kind: 'ptr', block, offset: Number(offset) } }
function cellPtr(cell, offset = 0) { return { kind: 'cellptr', cell, offset: Number(offset) } }
function isPointer(value) { return value && (value.kind === 'ptr' || value.kind === 'cellptr') }
function isSlice(value) { return value && value.kind === 'slice' }
function sliceValue(data, length) { return { kind: 'slice', data, length: Number(length) } }
function truthy(value) {
  if (typeof value === 'bigint') return value !== 0n
  if (typeof value === 'number') return value !== 0 && !Number.isNaN(value)
  return value !== null && value !== undefined
}
function asBigInt(value) {
  if (typeof value === 'bigint') return value
  if (typeof value === 'number' && Number.isFinite(value)) return BigInt(Math.trunc(value))
  if (value === false || value == null) return 0n
  if (value === true) return 1n
  throw new NqoError(`expected integer, got ${typeof value}`)
}
function checkedI64(value) {
  if (value < I64_MIN || value > I64_MAX) throw new NqoError('checked i64 overflow')
  return value
}
function widthForType(typeCode) {
  if (typeCode === 2 || typeCode === 6 || typeCode === 7) return 1
  if (typeCode === 8 || typeCode === 9) return 2
  if (typeCode === 10 || typeCode === 11) return 4
  return 8
}
function signedType(typeCode) { return [3, 6, 8, 10, 12, 14].includes(typeCode) }
function resolvePointer(pointer) {
  if (!isPointer(pointer)) throw new NqoError('null or non-pointer memory access')
  if (pointer.kind === 'ptr') return { block: pointer.block, offset: pointer.offset, cell: null }
  const { cell, offset } = pointer
  if (offset === 0 && !isPointer(cell.value)) return { block: null, offset: 0, cell }
  if (isPointer(cell.value)) {
    const nested = resolvePointer(cell.value)
    return { block: nested.block, offset: nested.offset + offset, cell: null }
  }
  if (!cell.block) cell.block = new MemoryBlock(Math.max(16, offset + 8))
  return { block: cell.block, offset, cell: null }
}
function writeScalar(pointer, value, width = 8) {
  const target = resolvePointer(pointer)
  if (target.cell && target.offset === 0) { target.cell.value = value; return }
  const block = target.block
  const offset = target.offset
  if (!block || offset < 0) throw new NqoError('invalid write address')
  block.ensure(offset + width)
  if (typeof value === 'object' && value !== null) {
    block.refs.set(offset, value)
    for (let i = 0; i < width; i++) block.buffer[offset + i] = 0
    return
  }
  block.refs.delete(offset)
  let raw = asBigInt(value)
  if (raw < 0) raw = (raw + (1n << BigInt(width * 8))) & ((1n << BigInt(width * 8)) - 1n)
  for (let i = 0; i < width; i++) block.buffer[offset + i] = Number((raw >> BigInt(i * 8)) & 0xffn)
}
function readScalar(pointer, width = 8, typeCode = 0) {
  const target = resolvePointer(pointer)
  if (target.cell && target.offset === 0) return target.cell.value ?? 0n
  const block = target.block
  const offset = target.offset
  if (!block || offset < 0 || offset + width > block.buffer.length) throw new NqoError('out-of-bounds memory read')
  if (block.refs.has(offset)) return block.refs.get(offset)
  let raw = 0n
  for (let i = 0; i < width; i++) raw |= BigInt(block.buffer[offset + i]) << BigInt(i * 8)
  if (signedType(typeCode)) {
    const bits = BigInt(width * 8)
    const sign = 1n << (bits - 1n)
    if (raw & sign) raw -= 1n << bits
  }
  return raw
}
function pointerAdd(base, index, width, immediate = 0n) {
  if (!isPointer(base)) throw new NqoError('pointer arithmetic on non-pointer')
  const delta = Number(asBigInt(index) * BigInt(Math.max(1, width)) + immediate)
  if (!Number.isSafeInteger(delta)) throw new NqoError('pointer offset exceeds host safe range')
  if (base.kind === 'ptr') return ptr(base.block, base.offset + delta)
  return cellPtr(base.cell, base.offset + delta)
}
function decodeStringLiteral(bytes) {
  let text = new TextDecoder().decode(bytes)
  if (text.length >= 2 && text[0] === '"' && text.at(-1) === '"') text = text.slice(1, -1)
  text = text.replace(/\\n/g, '\n').replace(/\\r/g, '\r').replace(/\\t/g, '\t').replace(/\\"/g, '"').replace(/\\\\/g, '\\')
  return new TextEncoder().encode(text)
}

export function loadNqo2(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input)
  if (bytes.length < 32 || String.fromCharCode(...bytes.subarray(0, 4)) !== 'NQO2') throw new NqoError('not an NQO2 module')
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const u32 = off => view.getUint32(off, true)
  const u64 = off => view.getBigUint64(off, true)
  const i64 = off => view.getBigInt64(off, true)
  const version = u32(4)
  if (version !== 2) throw new NqoError(`unsupported NQO version ${version}`)
  const sourceBytes = u32(8), functionCount = u32(12), instructionCount = u32(16)
  const functionBytes = u32(20), instructionBytes = u32(24)
  if (functionBytes !== 40 || instructionBytes !== 96) throw new NqoError('unsupported NQO2 record layout')
  const required = 32 + sourceBytes + functionCount * functionBytes + instructionCount * instructionBytes
  if (!Number.isSafeInteger(required) || required !== bytes.length) throw new NqoError(`invalid NQO2 length: expected ${required}, got ${bytes.length}`)
  const source = bytes.slice(32, 32 + sourceBytes)
  const decoder = new TextDecoder()
  let offset = 32 + sourceBytes
  const functions = []
  const bySymbol = new Map(), byName = new Map()
  for (let i = 0; i < functionCount; i++, offset += 40) {
    const fn = {
      symbol: u64(offset), nameStart: u32(offset + 8), nameEnd: u32(offset + 12),
      firstInstruction: u32(offset + 16), instructionCount: u32(offset + 20),
      parameterCount: u32(offset + 24), flags: u32(offset + 28)
    }
    if (fn.nameEnd <= fn.nameStart || fn.nameEnd > source.length || fn.firstInstruction + fn.instructionCount > instructionCount) throw new NqoError('invalid function record')
    fn.name = decoder.decode(source.subarray(fn.nameStart, fn.nameEnd))
    functions.push(fn); bySymbol.set(fn.symbol.toString(), fn); byName.set(fn.name, fn)
  }
  const instructions = []
  for (let i = 0; i < instructionCount; i++, offset += 96) {
    const bits = u64(offset + 80)
    const floatBuf = new ArrayBuffer(8); new DataView(floatBuf).setBigUint64(0, bits, true)
    instructions.push({
      opcode: u32(offset), typeCode: u32(offset + 4), flags: u32(offset + 8),
      dest: i64(offset + 16), left: i64(offset + 24), right: i64(offset + 32), immediate: i64(offset + 40),
      symbol: u64(offset + 48), width: Number(u64(offset + 56)), sourceStart: Number(u64(offset + 64)), sourceEnd: Number(u64(offset + 72)),
      floatImmediate: new DataView(floatBuf).getFloat64(0, true)
    })
  }
  const text = decoder.decode(source)
  const recordTypes = new Set([...text.matchAll(/\brecord\s+([A-Za-z_][A-Za-z0-9_]*)/g)].map(match => match[1]))
  return { version, bytes, source, sourceText: text, functions, instructions, bySymbol, byName, recordTypes }
}

export class NqoVm {
  constructor(module, options = {}) {
    this.module = module
    this.maxSteps = Number(options.maxSteps ?? 50_000_000)
    this.maxDepth = Number(options.maxDepth ?? 4096)
    this.stdout = options.stdout ?? (value => process.stdout.write(`${value}\n`))
    this.host = new Map(Object.entries(options.host ?? {}))
    this.steps = 0
  }
  sourceText(ins) {
    if (!ins || ins.sourceEnd <= ins.sourceStart || ins.sourceEnd > this.module.source.length) return ''
    return new TextDecoder().decode(this.module.source.subarray(ins.sourceStart, ins.sourceEnd))
  }
  zeroValueForType(text) {
    const type = text.trim()
    if (!type) throw new NqoError('zero-init marker has no type')
    if (type.startsWith('[]')) return sliceValue(null, 0)
    if (type.startsWith('*')) return null
    if (type.startsWith('[')) {
      const match = type.match(/^\[.*;\s*(\d+)\s*\]$/s)
      if (!match) throw new NqoError(`invalid zero-init array type ${type}`)
      const length = Number(match[1])
      if (!Number.isSafeInteger(length) || length < 0) throw new NqoError('array length exceeds host range')
      return ptr(new MemoryBlock(Math.min(length * 8, 1024), length), 0)
    }
    if (this.module.recordTypes.has(type.split('<')[0].trim())) return ptr(new MemoryBlock(128), 0)
    if (['float'].includes(type)) return 0
    if (['bool','int','i8','u8','i16','u16','i32','u32','i64','u64','isize','usize'].includes(type)) return 0n
    if (type === 'string') return new Uint8Array()
    // Generic locals are zeroed as scalar placeholders until monomorphization
    // gives them a concrete representation; unresolved ordinary calls never use
    // this path because they are not tagged by the zero-init normalization pass.
    if (/^[A-Z][A-Za-z0-9_]*$/.test(type)) return 0n
    throw new NqoError(`unsupported zero-init type ${type}`)
  }
  hostCall(name, args, ins) {
    if (this.host.has(name)) return this.host.get(name)(...args)
    if (name === 'len') {
      const value = args[0]
      if (isSlice(value)) return BigInt(value.length)
      if (value instanceof Uint8Array || Array.isArray(value) || typeof value === 'string') return BigInt(value.length)
      if (isPointer(value) && value.kind === 'ptr' && value.block.logicalLength != null) return BigInt(value.block.logicalLength)
      throw new NqoError('len requires a slice, array or string', ins)
    }
    if (name === 'slice') {
      const value = args[0]
      let length = args.length > 1 ? Number(asBigInt(args[1])) : null
      if (isSlice(value)) return sliceValue(value.data, length ?? value.length)
      if (value instanceof Uint8Array) return sliceValue(ptr(Object.assign(new MemoryBlock(value.length, value.length), { buffer: value }), 0), length ?? value.length)
      if (isPointer(value)) {
        const inferred = value.kind === 'ptr' ? value.block.logicalLength ?? value.block.buffer.length : null
        if (length == null) length = inferred
        if (length == null) throw new NqoError('slice(pointer) requires known length', ins)
        return sliceValue(value, length)
      }
      throw new NqoError('slice requires addressable storage', ins)
    }
    if (name === 'print') { this.stdout(args[0]); return 0n }
    const fn = this.module.byName.get(name)
    if (fn) return this.executeFunction(fn, args, 0)
    // Internal typed zero-init normalization generates a no-argument call whose
    // source span is exactly a type expression. Only demonstrable types qualify.
    if (args.length === 0 && (this.module.recordTypes.has(name.split('<')[0].trim()) || /^(?:\*|\[\]|\[)|^(?:bool|int|float|string|i8|u8|i16|u16|i32|u32|i64|u64|isize|usize)$/.test(name))) return this.zeroValueForType(name)
    throw new NqoError(`unresolved call ${name}`, ins)
  }
  getReg(frame, id, ins) {
    if (id < 0n) return undefined
    const key = id.toString()
    if (!frame.regs.has(key)) throw new NqoError(`read of undefined register ${key}`, ins)
    return frame.regs.get(key)
  }
  setReg(frame, id, value) { if (id >= 0n) frame.regs.set(id.toString(), value) }
  cell(frame, symbol) {
    const key = symbol.toString()
    if (!frame.cells.has(key)) frame.cells.set(key, { value: 0n, block: null })
    return frame.cells.get(key)
  }
  executeFunction(functionOrName, args = [], depth = 0) {
    const fn = typeof functionOrName === 'string' ? this.module.byName.get(functionOrName) : functionOrName
    if (!fn) throw new NqoError(`function not found: ${String(functionOrName)}`)
    if (depth > this.maxDepth) throw new NqoError('NQO call-depth limit exceeded')
    const frame = { regs: new Map(), cells: new Map(), pendingArgs: new Map(), args, pc: fn.firstInstruction }
    const end = fn.firstInstruction + fn.instructionCount
    while (frame.pc < end) {
      if (++this.steps > this.maxSteps) throw new NqoError('NQO instruction-step limit exceeded')
      const ins = this.module.instructions[frame.pc]
      const next = frame.pc + 1
      let value, a, b
      switch (ins.opcode) {
        case 0: case 44: break
        case 1: this.setReg(frame, ins.dest, ins.immediate); break
        case 2: this.setReg(frame, ins.dest, ins.floatImmediate); break
        case 3: this.setReg(frame, ins.dest, this.getReg(frame, ins.left, ins)); break
        case 4: this.setReg(frame, ins.dest, this.cell(frame, ins.symbol).value); break
        case 5: this.cell(frame, ins.symbol).value = this.getReg(frame, ins.left, ins); break
        case 6: case 7: case 8: case 9: case 10:
          a = this.getReg(frame, ins.left, ins); b = this.getReg(frame, ins.right, ins)
          if (typeof a === 'number' || typeof b === 'number') {
            a = Number(a); b = Number(b)
            if ((ins.opcode === 9 || ins.opcode === 10) && b === 0) throw new NqoError('division by zero', ins)
            value = ins.opcode === 6 ? a + b : ins.opcode === 7 ? a - b : ins.opcode === 8 ? a * b : ins.opcode === 9 ? a / b : a % b
          } else {
            a = asBigInt(a); b = asBigInt(b)
            if ((ins.opcode === 9 || ins.opcode === 10) && b === 0n) throw new NqoError('division by zero', ins)
            value = ins.opcode === 6 ? a + b : ins.opcode === 7 ? a - b : ins.opcode === 8 ? a * b : ins.opcode === 9 ? a / b : a % b
            if (ins.flags & 2) value = checkedI64(value)
          }
          this.setReg(frame, ins.dest, value); break
        case 11: a = this.getReg(frame, ins.left, ins); this.setReg(frame, ins.dest, typeof a === 'number' ? -a : checkedI64(-asBigInt(a))); break
        case 12: this.setReg(frame, ins.dest, truthy(this.getReg(frame, ins.left, ins)) ? 0n : 1n); break
        case 13: case 14: case 15: case 16: case 17: case 18:
          a = this.getReg(frame, ins.left, ins); b = this.getReg(frame, ins.right, ins)
          value = ins.opcode === 13 ? a === b : ins.opcode === 14 ? a !== b : ins.opcode === 15 ? a < b : ins.opcode === 16 ? a <= b : ins.opcode === 17 ? a > b : a >= b
          this.setReg(frame, ins.dest, value ? 1n : 0n); break
        case 19: frame.pc = Number(ins.immediate); continue
        case 20: if (!truthy(this.getReg(frame, ins.left, ins))) { frame.pc = Number(ins.immediate); continue } break
        case 21: {
          const callArgs = []
          for (let i = 0; i < Number(ins.width); i++) callArgs.push(frame.pendingArgs.get(i) ?? 0n)
          frame.pendingArgs.clear()
          const target = this.module.bySymbol.get(ins.symbol.toString())
          const name = this.sourceText(ins)
          value = target ? this.executeFunction(target, callArgs, depth + 1) : this.hostCall(name, callArgs, ins)
          this.setReg(frame, ins.dest, value)
          break
        }
        case 22: return ins.left >= 0n ? this.getReg(frame, ins.left, ins) : 0n
        case 23: throw new NqoError('phi requires CFG predecessor metadata and is not emitted by current Stage-2 lowering', ins)
        case 24: {
          const cell = this.cell(frame, ins.symbol)
          this.setReg(frame, ins.dest, isPointer(cell.value) ? cell.value : cellPtr(cell, 0))
          break
        }
        case 25: this.setReg(frame, ins.dest, pointerAdd(this.getReg(frame, ins.left, ins), this.getReg(frame, ins.right, ins), ins.width || 1, ins.immediate)); break
        case 26: case 28: case 29: this.setReg(frame, ins.dest, readScalar(this.getReg(frame, ins.left, ins), ins.width || 8, ins.typeCode)); break
        case 27: writeScalar(this.getReg(frame, ins.left, ins), this.getReg(frame, ins.right, ins), ins.width || 8); break
        case 30: break
        case 31: throw new NqoError(`unimplemented intrinsic ${ins.symbol}`, ins)
        case 32: throw new NqoError('inline assembly is unavailable in portable NQO2 execution', ins)
        case 33: {
          value = this.getReg(frame, ins.left, ins)
          if (typeof value === 'bigint' && value < 0n) return value
          this.setReg(frame, ins.dest, value)
          break
        }
        case 34: throw new NqoThrow(this.getReg(frame, ins.left, ins))
        case 35: {
          const index = asBigInt(this.getReg(frame, ins.left, ins)), length = asBigInt(this.getReg(frame, ins.right, ins))
          if (index < 0n || index >= length) throw new NqoError(`bounds check failed: ${index}/${length}`, ins)
          break
        }
        case 36: break
        case 37: {
          const block = new MemoryBlock(ins.width)
          this.setReg(frame, ins.dest, ptr(block, 0)); break
        }
        case 38: this.setReg(frame, ins.dest, readScalar(this.getReg(frame, ins.left, ins), ins.width || widthForType(ins.typeCode), ins.typeCode)); break
        case 39: writeScalar(this.getReg(frame, ins.left, ins), this.getReg(frame, ins.right, ins), ins.width || 8); break
        case 40: {
          const s = this.getReg(frame, ins.left, ins); if (!isSlice(s)) throw new NqoError('slice_data on non-slice', ins); this.setReg(frame, ins.dest, s.data); break
        }
        case 41: {
          const s = this.getReg(frame, ins.left, ins); if (!isSlice(s)) throw new NqoError('slice_len on non-slice', ins); this.setReg(frame, ins.dest, BigInt(s.length)); break
        }
        case 42: this.setReg(frame, ins.dest, sliceValue(this.getReg(frame, ins.left, ins), asBigInt(this.getReg(frame, ins.right, ins)))); break
        case 43: {
          value = this.getReg(frame, ins.left, ins)
          if (ins.typeCode === 4) value = Number(value)
          else if ([2,3,6,7,8,9,10,11,12,13,14,15].includes(ins.typeCode)) value = asBigInt(value)
          this.setReg(frame, ins.dest, value); break
        }
        case 45: {
          if (ins.sourceEnd > this.module.source.length || ins.sourceEnd <= ins.sourceStart) throw new NqoError('invalid string source span', ins)
          const literal = decodeStringLiteral(this.module.source.subarray(ins.sourceStart, ins.sourceEnd))
          const block = new MemoryBlock(literal.length, literal.length); block.buffer.set(literal)
          this.setReg(frame, ins.dest, sliceValue(ptr(block, 0), literal.length)); break
        }
        case 46: frame.pendingArgs.set(Number(ins.immediate), this.getReg(frame, ins.left, ins)); break
        case 47: this.setReg(frame, ins.dest, frame.args[Number(ins.immediate)] ?? 0n); break
        default: throw new NqoError(`unsupported NIR opcode ${ins.opcode}`, ins)
      }
      frame.pc = next
    }
    return 0n
  }
  invoke(name, ...args) {
    this.steps = 0
    try { return this.executeFunction(name, args, 0) }
    catch (error) { if (error instanceof NqoThrow) throw error; throw error }
  }
}

export function loadNqo2File(path, options = {}) {
  const module = loadNqo2(readFileSync(path))
  return new NqoVm(module, options)
}

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const [file, functionName = 'main', ...rawArgs] = process.argv.slice(2)
  if (!file) { console.error('usage: node Runtime/nqo-vm.mjs <module.nqo> [function] [integer-args...]'); process.exit(2) }
  try {
    const vm = loadNqo2File(file)
    const result = vm.invoke(functionName, ...rawArgs.map(value => BigInt(value)))
    if (result !== undefined) console.log(typeof result === 'bigint' ? result.toString() : result)
  } catch (error) {
    console.error(error?.stack || String(error)); process.exit(1)
  }
}
