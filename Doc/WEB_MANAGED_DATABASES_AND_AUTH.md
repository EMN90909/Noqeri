# Noqeri web: managed databases, reactive forms and authentication

**Status: experimental integration libraries (Node 22+ for backend; browser ES modules for client).** They do not change the native `noqeri-web serve-app` compiler, do not imply a production security audit, and do not introduce a JavaScript requirement for existing native applications.

The native web/database providers remain documented in [WEB_NATIVE_DATABASES.md](WEB_NATIVE_DATABASES.md). NoeSQL remains a separate native runtime and its storage/transactions are not replaced by these adapters.

## Which backend works where?

| Engine | Entry point | Capability | Qualification |
| --- | --- | --- | --- |
| NoeSQL | Existing native web adapter | NoeSQL .noe queries and commands | Separate native provider; current limits apply |
| SQLite | `openDatabase({engine:"sqlite",file})` | Prepared CRUD via Node `node:sqlite` | Unit-level interface; Node dependency |
| PostgreSQL / Neon | `openDatabase({engine:"neon",connectionString})` | Prepared CRUD via optional `pg` | Mock-client tests; no live Neon instance tested |
| Supabase | `openDatabase({engine:"supabase",...})` | PostgREST list/get/create/update/remove | Mock HTTP tests; real RLS/auth deployment unverified |
| Firebase Firestore | `openDatabase({engine:"firebase",...})` | Firestore REST typed document CRUD | Mock HTTP tests; real Firebase rules/auth unverified |
| MySQL / DuckDB | Existing native web adapters | SQL through optional C libraries | Existing native qualifications apply |

**Neobase:** This name is not treated as an alias for an undocumented provider. Neon is a PostgreSQL-compatible provider, whereas a specific Neobase product/API requires a separate confirmed contract. No compatibility with Neobase is claimed.

```js
// Server-side, never in the browser. Secrets come from environment/configuration.
import {openDatabase} from './Runtime/web-managed-databases.mjs'

// Per-user JWTs preserve Supabase RLS; avoid service-role keys in user endpoints.
const tasks = await openDatabase({
  engine: 'supabase',
  url: process.env.SUPABASE_URL,
  apiKey: process.env.SUPABASE_PUBLISHABLE_KEY,
  accessToken: () => currentRequestUserJwt // resolve independently for each request
})
const rows = await tasks.list('tasks',{limit:20})
await tasks.create('tasks',{title:'Plan release'})
await tasks.close()

// Firestore: pass a Firebase user ID token (rules enforced), or an authorized
// Google OAuth token for privileged server access. These differ in permissions.
const documents = await openDatabase({
  engine:'firebase',
  projectId:process.env.FIREBASE_PROJECT_ID,
  token:()=>currentRequestFirebaseIdToken
})
await documents.create('notes',{title:'Hello',done:false},{id:'first-note'})

// Neon: optional "pg" dependency; query values are always bind parameters.
const neon=await openDatabase({
  engine:'neon',
  connectionString:process.env.DATABASE_URL
})
const item=await neon.get('notes',7)
await neon.close()
```

All five CRUD methods share `list(table,{limit})`, `get(table,id)`, `create(table,record)`, `update(table,id,record)`, `remove(table,id)`. Firestore uses collection names/document IDs rather than relational tables/primary keys. Results are native JS records, not Noqeri Stage-2 typed objects. CRUD methods do **not** expose general SQL, multi-statement transactions, joins, subscriptions or schema migrations. Columns/table names are validated and SQL values bound; Firestore value types are a bounded JSON-compatible subset (including nested maps/arrays). Neither Firestore nor Supabase mock tests prove authorization rules.

## Noqeri browser forms and reactivity

Web views support optional `persist setting = "value"` browser-side primitive state and `subscribe changes from "/events"` same-origin WebSocket subscriptions. Persistent primitives use bounded `localStorage` and must **never** hold passwords, identity tokens, session data or other secrets. Subscriptions are attached when the view mounts and closed on disposal. Browser-side persistence does not synchronize across devices and is distinct from the durable SQLite **server** session store.\n\nWeb views support `on.input`, `on.change` and `on.submit` event actions, with a read-only `event` value that exposes `event.value`, `event.checked`, `event.valid` and `event.form.fieldName` for accepted form values; handled `on.submit` actions can also read `form.fieldName`. The runtime prevents normal page navigation for handled client-side submits; HTML required/length attributes are forwarded to the DOM. **Backend validation remains mandatory.**

```noqeri
import "noqeri/app" as app
let email = ""
let submitted = ""
route "/" {
  form on.submit { submitted = event.form.email } {
    label for: "email" { "Email" }
    input id: "email" name: "email" type: "email" required: true
          on.input { email = event.value } {}
    button type: "submit" { "Save" }
  }
  p { email }
  p { submitted }
}
```

`noqeri/app` is a bundled, opt-in browser module providing `signal(initial)`, `validate(values,rules)`, `resource(path)`, `request(path,options)`, `session(path)` and `socket(path,options)`. Its asynchronous operations use native browser promises; this view compiler does **not** yet provide typed await/error/result lowering. Do not claim form success or authentication until a response confirms it. The module refuses cross-origin URLs, oversized WebSocket messages and invalid request methods, and requires same-origin credential handling. It does not store tokens in browser localStorage.

WebSocket client URLs derive from the current origin and use `wss://` on HTTPS. Automatic reconnection uses capped exponential backoff; queued text messages are bounded; `close()` stops reconnection. The server must independently verify the WebSocket `Origin`, session cookie and per-message authorization; no automatic shared durable pub/sub or horizontally-scaled broadcast is supplied.

## Persistent sessions and password verification (Node server hosts)

```js
import {sessionManager,sqliteSessions,passwordAuth,hashPassword,requireSameOrigin}
  from './Runtime/noqeri-web-sessions.mjs'

const store = await sqliteSessions(process.env.NOQERI_SESSION_DB)
const sessions = sessionManager({
  secret: process.env.NOQERI_SESSION_SECRET, // 32+ random bytes
  store,
  secure: true,
  sameSite:'Lax'
})
const auth = passwordAuth({
  sessions,
  lookupUser: async identifier => await lookupAccount(identifier)
})
// Registration uses hashPassword(password), persisted to an account database.
// Login invokes auth.login(identifier, password), then sends result.cookie as Set-Cookie.
// GET /api/session checks sessions.get(req.headers.cookie) and returns only public
// session identity and csrf token, or {user:null,csrf:null}.
// Before POST/PATCH/DELETE: requireSameOrigin(req.headers.origin, configuredOrigin);
// and sessions.assertCsrf(session, req.headers['x-csrf-token']).
// Logout calls sessions.destroy(cookie), sends its expired Set-Cookie, and returns {}.
```

`sessionManager` provides `issue`, `get`, `destroy`, `rotate`, and `assertCsrf`. Cookies are unpredictable HMAC-authenticated tokens using HttpOnly, SameSite and (by default) Secure. The server-side session record contains only a small public identity, a random CSRF token and expiry. `sqliteSessions` offers a WAL-enabled durable store; the memory implementation forgets sessions on restart. Session signing secrets **must** be stable, safely stored, and rotated with a deliberate migration/logout policy. The SQLite store must live in a protected filesystem location; it is not encrypted at rest. Rate limiting, account lockout, email verification, password reset, social OAuth, multi-factor auth, TLS termination, reverse-proxy trust, authorization rules, device revocation, backups and production hardening remain the **host application's responsibility**.

**Important:** The Supabase/Firebase/Neon integrations are server-side ES-module adapters. They are not currently compilable through native Stage-2 typed NIR, and are not automatically exposed to Noqeri native route declarations. Server-host API endpoints must be implemented explicitly. Do not place Supabase service-role tokens, Firebase admin credentials or Neon passwords in `.noqeri` client files or web import maps.

## Tests

```sh
node --test tests/web-managed-databases.test.mjs tests/web-sessions.test.mjs tests/web-browser-data.test.mjs
node --test tests/web-framework.test.mjs tests/web-views.test.mjs
```

Tests use fake provider clients and mocked HTTP; live-provider qualification (Supabase RLS/Firebase Security Rules, Postgres TLS, session persistence across multiple processes and browser interoperability) remains future work.

## Provider privilege requirements (October 2026)

The Node Supabase adapter refuses `sb_secret_*` keys unless `allowPrivilegedKey:true` is explicitly supplied and refuses combining such keys with `accessToken`. A publishable key plus independently validated request JWT remains the intended RLS path. Both the Node and native adapters require host-side authentication, authorization and tenant isolation. Native `auth_env` is a single server identity shared by all requests, not per-visitor authentication. The native `firestore` preview is text-only; the Node adapter supports tagged JSON values. Firebase Realtime Database is independent of Cloud Firestore.
