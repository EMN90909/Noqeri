# Native cloud databases and reactive browser features (developer preview)

This change extends the **existing two web modes**, not a new unified compiler:

- `noqeri-web serve-app`: server-side credentials, database-backed pages, CSRF-protected form posts, Basic authentication, in-memory sessions, and durable NQKV application variables.
- Browser view compiler (`Runtime/web-view-compiler.mjs`): reactive events, persisted nonsecret UI primitives and same-origin WebSocket subscriptions.

These remain experimental. The browser compiler emits JavaScript; the native C++ bootstrap server is loopback-only and does not implement a WebSocket upgrade server or OAuth login.

## Database providers

The existing `noesql`, `sqlite`, `postgresql`, `duckdb` and `mysql` engines remain unchanged. The new providers are:

| Engine | Transport | Configuration | Scope |
| --- | --- | --- | --- |
| `supabase` | HTTPS/PostgREST via libcurl | `dsn_env` for Data API root, `key_env` for bearer/API key | JSON GET and bounded POST/PATCH/DELETE |
| `firebase` | HTTPS Realtime Database REST via libcurl | `dsn_env` for RTDB URL, `key_env` for Google OAuth access token | JSON GET/POST/PUT/PATCH/DELETE, `.json` paths |
| `neon` | PostgreSQL/libpq | `dsn_env` for connection string | PostgreSQL prepared queries, requires `sslmode=verify-full` |

`NeoBase` (neobase.cloud) is an AI database copilot, not a database storage engine; use the actual PostgreSQL/MySQL/etc. driver when accessing a database managed via NeoBase. Other systems such as MongoDB and Firestore are **not supported** by these adapters.

### Supabase, native server

```sh
export SUPABASE_REST_URL='https://YOUR_PROJECT.supabase.co/rest/v1'
export SUPABASE_SERVER_TOKEN='YOUR_SERVER_TOKEN'
cmake -S . -B build -DNOQERI_WEB_CLOUD_DRIVERS=ON
cmake --build build --target noqeri-web
./build/noqeri-web drivers
./build/noqeri-web serve-app examples/native_web_supabase 8080
```

`examples/native_web_supabase/main.noqeri`:

```noqeri
database {
  engine: "supabase"
  dsn_env: "SUPABASE_REST_URL"
  key_env: "SUPABASE_SERVER_TOKEN"
}
query tasks = "GET /tasks?select=id,title"
route "/" {
  main {
    h1 { "Tasks" }
    pre { tasks }
    form on.submit {
      validate(title, 1, 80)
      db.execute("POST /tasks", title: form.title)
    } {
      input name: "title" type: "text" required: true {}
      button type: "submit" { "Add task" }
    }
  }
}
```

REST source strings are trusted compile-time literals; form values are passed as named JSON **string** fields and are never interpolated into URLs. For GET, embed filters in the trusted path only. Avoid using a Supabase `service_role` key for public-facing sites: elevated keys can bypass Row Level Security. This preview has no per-user Supabase login/JWT propagation. Choose a least-privilege backend credential and enforce authorization yourself.

### Firebase Realtime Database, native server

```sh
export FIREBASE_DATABASE_URL='https://YOUR_PROJECT-default-rtdb.firebaseio.com'
export FIREBASE_OAUTH_TOKEN='SHORT_LIVED_GOOGLE_OAUTH_ACCESS_TOKEN'
./build/noqeri-web serve-app examples/native_web_firebase 8080
```

Use `engine: "firebase"`, `dsn_env: "FIREBASE_DATABASE_URL"`, `key_env: "FIREBASE_OAUTH_TOKEN"`, `query items = "GET /items.json"` and `db.execute("POST /items.json", title: form.title)`. Tokens must be obtained and refreshed externally; this connector does **not** implement Google OAuth, Firebase Authentication, Firestore query syntax, or an ID-token exchange.

### Neon, native PostgreSQL

```sh
export NEON_DATABASE_URL='host=... dbname=... user=... password=... sslmode=verify-full sslrootcert=/path/to/ca.pem'
```

Then declare `database { engine: "neon" dsn_env: "NEON_DATABASE_URL" }`, and use prepared SQL with `:name` placeholders. Existing query limits and native SQL result serialization apply. The `sslmode=verify-full` setting is enforced, and a CA trust chain must be configured for your deployment. No HTTP-based Neon driver is shipped.

### Security boundaries

The native cloud adapter fails closed if libcurl is absent, requires HTTPS with verified certificates, never follows redirects, limits requests to 10 seconds and responses to 64 KiB, and does not print credentials on request errors. Keep URLs and tokens in **server environment variables**; do not place them in Noqeri source files, browser views, or `web.importmap.json`.

The native server binds to `127.0.0.1`. For externally reachable deployments, use an HTTPS reverse proxy; set `NOQERI_WEB_PUBLIC_ORIGIN`, `NOQERI_WEB_SECURE_COOKIE=1`, `NOQERI_WEB_REQUIRE_ORIGIN=1` and optionally `NOQERI_WEB_BASIC_USER` and `NOQERI_WEB_BASIC_PASSWORD`. Sessions remain RAM-only with bounded expiry, while declared `persist` fields in native applications use the separate NQKV store. These features do **not** constitute complete account registration, password hashing, OAuth, RBAC or production authentication.

## Reactive browser applications

```noqeri
persist nickname = "Guest"
subscribe updates from "/ws/updates"
view {
  h1 { "Welcome, " + nickname }
  form on.submit { nickname = form.nickname } {
    input name: "nickname" required: true minlength: 2 maxlength: 40 {}
    button type: "submit" { "Save" }
  }
  p { updates.connected }
  p { updates.data }
}
```

- `persist` saves **string, boolean, or numeric** UI values to origin-local `localStorage` with a stable `noqeri:v1:` prefix. Storage may be unavailable and is user-modifiable: it is **not** trusted backend persistence. Never persist passwords, access tokens, roles, or private data.
- `input.value` and `input.checked` in event handlers, plus `form.FIELD` in submit handlers, read bounded values rather than exposing arbitrary browser globals. `required`, `minlength`, `maxlength`, `min`, `max`, `step`, `autocomplete`, and `inputmode` are allowed HTML attributes.
- `subscribe name from "/fixed/path"` uses a same-origin `ws:`/`wss:` endpoint, parses JSON text messages (64 KiB maximum), updates `name.connected`, `name.data`, and `name.error`, retries with backoff, and closes on disposal. WebSocket transport must be provided by an independent server or gateway; `noqeri-web serve-app` **does not** serve this endpoint. Do not accept cross-origin subscriptions or append bearer credentials to the WebSocket URL.

## Verification

```sh
node --test tests/web-views.test.mjs tests/web-framework.test.mjs
python3 tests/native_web_cloud.py ./build/noqeri-web
ctest --test-dir build -R 'noqeri-native-web' --output-on-failure
```

These tests validate frontend state and transport behavior under test doubles and native cloud fail-closed configuration. They are **not** live-provider CRUD, security policy, multiuser authorization, real WebSocket handshake, or production TLS/load certification.


## Native server form and session hardening

The native web server now accepts three independent, authoritative server-action validation helpers:
- validate(field, minimum_bytes, maximum_bytes)
- validateEmail(field): conservative ASCII email shape validation, not deliverability or full RFC mailbox validation
- validateNumber(field, minimum, maximum): unsigned decimal integer with inclusive bounds from 0 to 1,000,000

Native forms also allow required, minlength, maxlength, min, max and autocomplete HTML attributes. Browser field attributes improve UX, but the server checks the declared validators **before** any state or database mutation. Validation failures return HTTP 422 and do not rotate CSRF tokens or commit changes.

For progressive native form updates, set NOQERI_WEB_REACTIVE=1. This explicitly enables a tiny same-origin Fetch script at /__nq/reactive.js, which intercepts only framework-owned form POSTs and replaces the page body with newly rendered HTML after the server's 303 redirect (or shows the server's 422 error page). Responses get a restrictive script-src self CSP. The default remains no client JavaScript. This mode does not implement a client signals compiler, optimistic mutations or a native WebSocket broadcaster; browser view subscriptions above still require a separate WebSocket server.

Optional private-service authentication: NOQERI_WEB_BEARER_TOKEN must be a high-entropy 32..512-byte token and is mutually exclusive with the Basic-auth configuration. Its Authorization header is checked in constant time. It is a single process-wide service gate, **not** individual user login, RBAC, Supabase Auth or Firebase Auth. Keep the token server side. Existing CSRF rotation, same-origin checks, session idle expiry, and HttpOnly/SameSite cookie behavior remain in place.

The native .nqkv persistence path now uses exclusive mode-0600 temporary-file creation, an fsync of file contents, atomic rename, and parent-directory fsync on POSIX. This improves ordinary local crash durability and secrecy; it is **not** a replicated multi-host session database or a transactional store coupled to cloud database writes.

To verify without any third-party cloud credentials:
    python3 tests/native_web_reactive_security.py ./build/noqeri-web
