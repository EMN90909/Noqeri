const IDENT = /^[A-Za-z_][A-Za-z0-9_-]*$/
const NUMBER = /^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$/
const RESERVED = new Set(['true', 'false', 'null'])

class NqonParser {
  constructor(text, source = '<nqon>') {
    this.text = text
    this.source = source
    this.i = 0
    this.line = 1
    this.column = 1
  }

  fail(message) {
    const error = new SyntaxError(`${this.source}:${this.line}:${this.column}: ${message}`)
    error.code = 'NQON_PARSE_ERROR'
    throw error
  }

  peek(offset = 0) { return this.text[this.i + offset] }
  eof() { return this.i >= this.text.length }

  advance() {
    const ch = this.text[this.i++]
    if (ch === '\n') { this.line++; this.column = 1 } else this.column++
    return ch
  }

  skipTrivia() {
    for (;;) {
      while (!this.eof() && /\s/.test(this.peek())) this.advance()
      if (this.peek() === '/' && this.peek(1) === '/') {
        this.advance(); this.advance()
        while (!this.eof() && this.peek() !== '\n') this.advance()
        continue
      }
      if (this.peek() === '/' && this.peek(1) === '*') {
        this.advance(); this.advance()
        let closed = false
        while (!this.eof()) {
          if (this.peek() === '*' && this.peek(1) === '/') {
            this.advance(); this.advance(); closed = true; break
          }
          this.advance()
        }
        if (!closed) this.fail('unterminated block comment')
        continue
      }
      break
    }
  }

  parse() {
    this.skipTrivia()
    const value = this.peek() === '{' ? this.parseObject(true) : this.parseEntries(null)
    this.skipTrivia()
    if (!this.eof()) this.fail(`unexpected '${this.peek()}'`)
    return value
  }

  parseEntries(stopChar) {
    const out = Object.create(null)
    for (;;) {
      this.skipTrivia()
      if (stopChar && this.peek() === stopChar) { this.advance(); return out }
      if (this.eof()) {
        if (stopChar) this.fail(`expected '${stopChar}'`)
        return out
      }
      const key = this.parseKey()
      this.skipTrivia()
      let value
      if (this.peek() === '=') {
        this.advance()
        value = this.parseValue()
      } else if (this.peek() === '{') {
        value = this.parseObject(true)
      } else {
        this.fail(`expected '=' or '{' after key '${key}'`)
      }
      if (Object.prototype.hasOwnProperty.call(out, key)) this.fail(`duplicate key '${key}'`)
      out[key] = value
      this.skipTrivia()
      if (this.peek() === ',') this.advance()
    }
  }

  parseKey() {
    this.skipTrivia()
    if (this.peek() === '"') return this.parseString()
    const start = this.i
    if (!/[A-Za-z_]/.test(this.peek() || '')) this.fail('expected a key')
    this.advance()
    while (!this.eof() && /[A-Za-z0-9_-]/.test(this.peek())) this.advance()
    return this.text.slice(start, this.i)
  }

  parseObject(open = false) {
    this.skipTrivia()
    if (open) {
      if (this.peek() !== '{') this.fail("expected '{'")
      this.advance()
    }
    return this.parseEntries(open ? '}' : null)
  }

  parseArray() {
    if (this.peek() !== '[') this.fail("expected '['")
    this.advance()
    const out = []
    for (;;) {
      this.skipTrivia()
      if (this.peek() === ']') { this.advance(); return out }
      if (this.eof()) this.fail("expected ']'")
      out.push(this.parseValue())
      this.skipTrivia()
      if (this.peek() === ',') this.advance()
    }
  }

  parseValue() {
    this.skipTrivia()
    const ch = this.peek()
    if (ch === '"') return this.parseString()
    if (ch === '{') return this.parseObject(true)
    if (ch === '[') return this.parseArray()
    return this.parseBare()
  }

  parseString() {
    if (this.peek() !== '"') this.fail('expected string')
    this.advance()
    let out = ''
    while (!this.eof()) {
      const ch = this.advance()
      if (ch === '"') return out
      if (ch === '\n' || ch === '\r') this.fail('newline in string literal')
      if (ch !== '\\') { out += ch; continue }
      if (this.eof()) this.fail('unterminated string escape')
      const esc = this.advance()
      const map = { '"': '"', '\\': '\\', '/': '/', b: '\b', f: '\f', n: '\n', r: '\r', t: '\t' }
      if (Object.prototype.hasOwnProperty.call(map, esc)) { out += map[esc]; continue }
      if (esc === 'u') {
        let hex = ''
        for (let n = 0; n < 4; n++) {
          if (this.eof()) this.fail('incomplete unicode escape')
          hex += this.advance()
        }
        if (!/^[0-9A-Fa-f]{4}$/.test(hex)) this.fail('invalid unicode escape')
        out += String.fromCharCode(Number.parseInt(hex, 16))
        continue
      }
      this.fail(`unknown string escape \\${esc}`)
    }
    this.fail('unterminated string literal')
  }

  parseBare() {
    const start = this.i
    while (!this.eof()) {
      const ch = this.peek()
      if (/\s/.test(ch) || ch === ',' || ch === ']' || ch === '}') break
      if (ch === '/' && (this.peek(1) === '/' || this.peek(1) === '*')) break
      if (ch === '=' || ch === '{' || ch === '[') break
      this.advance()
    }
    if (this.i === start) this.fail('expected a value')
    const token = this.text.slice(start, this.i)
    if (token === 'true') return true
    if (token === 'false') return false
    if (token === 'null') return null
    if (NUMBER.test(token)) {
      const value = Number(token)
      if (!Number.isFinite(value)) this.fail(`number is outside the portable range: ${token}`)
      return value
    }
    return token
  }
}

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) && (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null)
}

function keyText(key) { return IDENT.test(key) ? key : JSON.stringify(key) }

function stringText(value) {
  if (value.length > 0 && !RESERVED.has(value) && !NUMBER.test(value) && /^[A-Za-z0-9_./@:+-]+$/.test(value)) return value
  return JSON.stringify(value)
}

function scalarText(value, depth) {
  if (typeof value === 'string') return stringText(value)
  if (typeof value === 'number') {
    if (!Number.isFinite(value)) throw new TypeError('NQON cannot encode non-finite numbers')
    return String(value)
  }
  if (typeof value === 'boolean') return value ? 'true' : 'false'
  if (value === null) return 'null'
  if (Array.isArray(value)) return arrayText(value, depth)
  if (isPlainObject(value)) return objectText(value, depth, true)
  throw new TypeError(`NQON cannot encode ${typeof value}`)
}

function arrayText(values, depth) {
  if (values.length === 0) return '[]'
  const indent = '    '.repeat(depth)
  const child = '    '.repeat(depth + 1)
  return '[\n' + values.map(value => `${child}${scalarText(value, depth + 1)}`).join('\n') + `\n${indent}]`
}

function objectText(value, depth, braces) {
  const entries = Object.entries(value)
  if (entries.length === 0) return braces ? '{}' : ''
  const indent = '    '.repeat(depth)
  const child = '    '.repeat(depth + (braces ? 1 : 0))
  const lines = []
  for (const [key, item] of entries) {
    if (isPlainObject(item)) {
      const body = objectText(item, depth + (braces ? 1 : 0), true)
      lines.push(`${child}${keyText(key)} ${body}`)
    } else {
      lines.push(`${child}${keyText(key)} = ${scalarText(item, depth + (braces ? 1 : 0))}`)
    }
  }
  if (!braces) return lines.join('\n')
  return '{\n' + lines.join('\n') + `\n${indent}}`
}

export function parseNqon(text, options = {}) {
  if (typeof text !== 'string') throw new TypeError('parseNqon expects text')
  return new NqonParser(text, options.source || '<nqon>').parse()
}

export function stringifyNqon(value) {
  if (!isPlainObject(value)) throw new TypeError('an NQON document must be an object')
  const text = objectText(value, 0, false)
  return text ? text + '\n' : ''
}
