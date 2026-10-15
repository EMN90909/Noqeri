// No third-party dependencies. Run with: node tests/noqerisql_integration.mjs /path/to/noqeri
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
const executable = process.argv[2];
const standalone = process.argv.includes("--standalone");
if (!executable) throw Error("noqeri executable is required");
const dir = mkdtempSync(join(tmpdir(), "noqerisql-test-"));
const db = join(dir, "testing.sqlite");
const args = (file, database = db) => [...(standalone ? [] : ["sql"]), "sqlite", database, file];
function run(sql, database = db) {
  const path = join(dir, "statement.sql");
  writeFileSync(path, sql);
  return spawnSync(executable, args(path, database), { encoding: "utf8", timeout: 15000 });
}
function checkSuccess(r, label) {
  assert.equal(r.status, 0, label + ": " + (r.stderr || r.error));
}
function checkFailure(r, label) {
  assert.notEqual(r.status, 0, label + " unexpectedly succeeded");
}
function runAsync(path) {
  return new Promise((resolve, reject) => {
    const p = spawn(executable, args(path), { stdio: ["ignore", "pipe", "pipe"] });
    let stderr = "";
    p.stderr.on("data", chunk => { stderr += chunk; });
    p.stdout.resume();
    p.on("error", reject);
    p.on("exit", code => code === 0 ? resolve() : reject(Error("concurrent writer failed: " + stderr)));
  });
}
try {
  checkSuccess(run("CREATE TABLE counter (id INTEGER PRIMARY KEY, n INTEGER NOT NULL); INSERT INTO counter VALUES(1, 0);"), "schema");
  const inc = join(dir, "increment.sql");
  writeFileSync(inc, "UPDATE counter SET n=n+1 WHERE id=1;");
  const count = 64;
  await Promise.all(Array.from({ length: count }, () => runAsync(inc)));
  let r = run("SELECT n FROM counter WHERE id=1;");
  checkSuccess(r, "counter query");
  assert.match(r.stdout, /\b64\b/, "64 concurrent updates must not be lost");

  checkFailure(run("INSERT INTO counter VALUES(9, 1); INSERT INTO counter VALUES(9, 2);"), "duplicate-key rollback");
  r = run("SELECT count(*) AS c FROM counter WHERE id=9;");
  checkSuccess(r, "rollback verification");
  assert.match(r.stdout, /\b0\b/, "failed script must not persist partial inserts");

  checkFailure(run("INSERT INTO counter VALUES(10, 5); COMMIT; INSERT INTO counter VALUES(11, 6);"), "transaction escape");
  r = run("SELECT count(*) AS c FROM counter WHERE id=10;");
  checkSuccess(r, "COMMIT rollback verification");
  assert.match(r.stdout, /\b0\b/);

  checkFailure(run("SELECT ?;"), "unbound placeholder");
  checkFailure(run("PRAGMA writable_schema=ON;"), "unsafe PRAGMA");
  checkFailure(run("BEGIN;"), "nested transaction");

  checkSuccess(run("CREATE TABLE parent(id INTEGER PRIMARY KEY); CREATE TABLE child(parent_id INTEGER NOT NULL REFERENCES parent(id));"), "FK schema");
  checkFailure(run("INSERT INTO child VALUES(999);"), "foreign-key constraint");

  const corrupt = join(dir, "invalid.sqlite");
  writeFileSync(corrupt, "intentionally invalid database file, not a SQLite header");
  checkFailure(run("SELECT 1;", corrupt), "database corruption");

  const big = join(dir, "big.sql");
  writeFileSync(big, "SELECT 1;\n" + " ".repeat(1024 * 1024 + 1));
  const res = spawnSync(executable, args(big), { encoding: "utf8" });
  checkFailure(res, "oversized script");

  console.log("NoqeriSQL integration: 64 concurrent writes, rollback, transaction boundaries, foreign keys, corruption and limits PASS");
} finally {
  rmSync(dir, { recursive: true, force: true });
}
