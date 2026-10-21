const SAFE_BARE = /^[A-Za-z_][A-Za-z0-9_.\/-]*$/
const RESERVED = new Set(['true', 'false', 'null'])

export class NqonSyntaxError extends SyntaxError {
  constructor(message, line, column) {
    super(`${message} at ${line}:${column}`)
    this.name = 'NqonSyntaxError'
    this.line = line
    this.column = column
  }
}

class Parser {
  constructor(text, { maxDepth = 128 } = {}) {
    if (typeof text !== 'string') throw new TypeError('NQON input must be a string')
    this.text = text
    this.i = 0
    this.line = 1
    this.column = 1
    this.maxDepth = maxDepth
  }

  error(message) { throw new NqonSyntaxError(message, this.line, this.column) }
  eof() { return this.i >= this.text.length }
  peek(offset = 0) { return this.text[this.i + offset] ?? '' }
  take() {
    const c = this.peek()
    if (!c) return ''
    this.i++
    if (c === '\n') { this.line++; this.column = 1 } else this.column++
    return c
  }

  trivia() {
    for (;;) {
      while (/\s/.test(this.peek())) this.take()
      if (this.peek() === '/' && this.peek(1) === '/') {
        while (!this.eof() && this.take() !== '\n') {}
        continue
      }
      break
    }
  }

  key() {
    this.trivia()
    const start = this.i
    if (!/[A-Za-z_]/.test(this.peek())) this.error('expected key')
    this.take()
    while (/[A-Za-z0-9_-]/.test(this.peek())) this.take()
    return this.text.slice(start, this.i)
  }

  quoted() {
    const startLine = this.line, startColumn = this.column, start = this.i
    this.take()
    let escaped = false
    while (!this.eof()) {
      const c = this.take()
      if (escaped) { escaped = false; continue }
      if (c === '\\') { escaped = true; continue }
      if (c === '"') {
        try { return JSON.parse(this.text.slice(start, this.i)) }
        catch { throw new NqonSyntaxError('invalid string escape', startLine, startColumn) }
      }
      if (c === '\n' || c === '\r') throw new NqonSyntaxError('unterminated string', startLine, startColumn)
    }
    throw new NqonSyntaxError('unterminated string', startLine, startColumn)
  }

  bare() {
    const start = this.i
    while (!this.eof()) {
      const c = this.peek()
      if (/\s/.test(c) || c === ']' || c === '}' || c === ',' || (c === '/' && this.peek(1) === '/')) break
      this.take()
    }
    if (this.i === start) this.error('expected value')
    const token = this.text.slice(start, this.i)
    if (token === 'true') return true
    if (token === 'false') return false
    if (token === 'null') return null
    if (/^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(token)) return Number(token)
    if (!/^[A-Za-z_][A-Za-z0-9_.\/-]*$/.test(token)) this.error(`invalid bare value ${JSON.stringify(token)}`)
    return token
  }

  value(depth) {
    if (depth > this.maxDepth) this.error('maximum nesting depth exceeded')
    this.trivia()
    if (this.peek() === '"') return this.quoted()
    if (this.peek() === '[') return this.array(depth + 1)
    if (this.peek() === '{') return this.object(depth + 1, true)
    return this.bare()
  }

  array(depth) {
    const out = []
    this.take()
    for (;;) {
      this.trivia()
      if (this.peek() === ']') { this.take(); return out }
      if (this.eof()) this.error('unterminated array')
      out.push(this.value(depth))
      this.trivia()
      if (this.peek() === ',') this.take()
    }
  }

  object(depth, braced) {
    if (depth > this.maxDepth) this.error('maximum nesting depth exceeded')
    const out = Object.create(null)
    if (braced) this.take()
    for (;;) {
      this.trivia()
      if (braced && this.peek() === '}') { this.take(); return out }
      if (this.eof()) {
        if (braced) this.error('unterminated object')
        return out
      }
      const key = this.key()
      if (Object.hasOwn(out, key)) this.error(`duplicate key ${JSON.stringify(key)}`)
      this.trivia()
      if (this.peek() === '=') { this.take(); out[key] = this.value(depth + 1) }
      else if (this.peek() === '{') out[key] = this.object(depth + 1, true)
      else this.error("expected '=' or '{' after key")
      this.trivia()
      if (this.peek() === ',') this.take()
    }
  }

  parse() {
    const value = this.object(0, false)
    this.trivia()
    if (!this.eof()) this.error('unexpected trailing input')
    return value
  }
}

export function parseNqon(text, options) { return new Parser(text, options).parse() }

function quote(value) { return JSON.stringify(value) }
function scalar(value) {
  if (typeof value === 'string') return SAFE_BARE.test(value) && !RESERVED.has(value) ? value : quote(value)
  if (typeof value === 'number') {
    if (!Number.isFinite(value)) throw new TypeError('NQON cannot encode non-finite numbers')
    return String(value)
  }
  if (typeof value === 'boolean') return value ? 'true' : 'false'
  if (value === null) return 'null'
  throw new TypeError(`unsupported NQON scalar: ${typeof value}`)
}

export function stringifyNqon(value, { indent = '    ' } = {}) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new TypeError('NQON document root must be an object')
  const seen = new WeakSet()

  function emitValue(item, depth) {
    if (Array.isArray(item)) {
      if (seen.has(item)) throw new TypeError('NQON cannot encode cyclic arrays')
      seen.add(item)
      if (item.length === 0) { seen.delete(item); return '[]' }
      const pad = indent.repeat(depth), child = indent.repeat(depth + 1)
      const lines = ['[']
      for (const entry of item) {
        if (entry && typeof entry === 'object' && !Array.isArray(entry)) lines.push(emitObject(entry, depth + 1, true))
        else lines.push(`${child}${emitValue(entry, depth + 1)}`)
      }
      lines.push(`${pad}]`)
      seen.delete(item)
      return lines.join('\n')
    }
    return scalar(item)
  }

  function emitEntries(object, depth) {
    const lines = []
    for (const [key, item] of Object.entries(object)) {
      if (!/^[A-Za-z_][A-Za-z0-9_-]*$/.test(key)) throw new TypeError(`invalid NQON key ${JSON.stringify(key)}`)
      const pad = indent.repeat(depth)
      if (item && typeof item === 'object' && !Array.isArray(item)) {
        if (seen.has(item)) throw new TypeError('NQON cannot encode cyclic objects')
        seen.add(item)
        lines.push(`${pad}${key} {`)
        lines.push(...emitEntries(item, depth + 1))
        lines.push(`${pad}}`)
        seen.delete(item)
      } else lines.push(`${pad}${key} = ${emitValue(item, depth)}`)
    }
    return lines
  }

  function emitObject(object, depth, braced) {
    if (seen.has(object)) throw new TypeError('NQON cannot encode cyclic objects')
    seen.add(object)
    const lines = []
    const pad = indent.repeat(depth)
    if (braced) lines.push(`${pad}{`)
    lines.push(...emitEntries(object, depth + (braced ? 1 : 0)))
    if (braced) lines.push(`${pad}}`)
    seen.delete(object)
    return lines.join('\n')
  }

  return emitObject(value, 0, false) + '\n'
}
