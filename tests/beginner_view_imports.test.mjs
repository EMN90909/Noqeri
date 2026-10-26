import test from 'node:test'
import assert from 'node:assert/strict'
import { parseView } from '../Runtime/web-view-compiler.mjs'

test('browser views accept the canonical quoted package import', () => {
  const quoted=parseView('import "noqeri/ui" as ui\nroute "/" { main { ui.Heading("Hello") } }')
  const legacy=parseView('import noqeri/ui as ui\nroute "/" { main { ui.Heading("Hello") } }')
  assert.deepEqual(quoted.imports,legacy.imports)
  assert.deepEqual(quoted.routes,legacy.routes)
  assert.equal(quoted.imports[0].specifier,'noqeri/ui')
  assert.equal(quoted.imports[0].alias,'ui')
})
test('browser views reject unknown quoted package names', () => {
  assert.throws(()=>parseView('import "other/lib" as lib\nview { h1 { "Hi" } }'),
                /quoted module name/)
})
