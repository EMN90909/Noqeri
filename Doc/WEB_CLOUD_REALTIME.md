# Native cloud databases and reactive browser features (developer preview)

This change extends the **existing two web modes**, not a new unified compiler:

- `noqeri-web serve-app`: server-side credentials, database-backed pages, CSRF-protected form posts, Basic authentication, in-memory sessions, and durable NQKV application variables.
- Browser view compiler (`Runtime/web-view-compiler.mjs`): reactive events, persisted nonsecret UI primitives and same-origin WebSocket subscriptions.

These remain experimental. The browser compiler emits JavaScript; the native C++ bootstrap server is loopback-only and does not implement a WebSocket upgrade server or OAuth login.

## Database providers

The existing `noesql`, `sqlite`, `postgresql`, `duckdb` and `mysql` engines remain unchanged. The new providers are:

| Engine | Transport | Configuration | Scope |
| --- | --- | --- | --- |
| `supabase` | HTTPS/PostgREST via libcurl | `dsn_env` for Data API root, `key_env` for API key, optional `auth_env` fixed server JWT | JSON GET and bounded POST/PATCH/DELETE |
| `firebase` | HTTPS Realtime Database REST via libcurl | `dsn_env` for RTDB URL, `key_env` for Google OAuth access token | JSON GET/POST/PUT/PATCH/DELETE, `.json` paths |
| `neon` | PostgreSQL/libpq | `dsn_env` for connection string | PostgreSQL prepared queries, requires `sslmode=verify-full` |
| `firestore` | HTTPS Cloud Firestore REST via libcurl | `dsn_env` for Firestore documents API base, `key_env` for Firebase ID token or Google OAuth access token | Collection/document GET and text-only POST/PATCH/DELETE |

`NeoBase` (neobase.cloud) is an AI database copilot, not a database storage engine; use the actual PostgreSQL/MySQL/etc. driver when accessing a database managed via NeoBase. MongoDB remains unsupported as a direct native engine. Firestore has a **separate** native, text-only REST adapter; it is not an alias for Firebase Realtime Database.

### Supabase, native server

```sh
export SUPABASE_REST_URL='https://YOUR_PROJECT.supabase.co/rest/v1'
export SUPABASE_PUBLISHABLE_KEY='sb_publishable_...'
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
  key_env: "SUPABASE_PUBLISHABLE_KEY"
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

REST source strings are trusted compile-time literals; form values are passed as named JSON **string** fields and are never interpolated into URLs. For GET, embed filters in the trusted path only. Avoid using a Supabase `service_role` key for public-facing sites: elevated keys can bypass Row Level Security. New publishable keys travel in the `apikey` header, **not** as a bearer token. Legacy JWT-format API keys still use the existing bearer compatibility behavior. Optional `auth_env` names a **fixed server-side JWT**, not a dynamically authenticated browser user. Every visitor would share that principal, so this setting must not be used as per-user login/RLS support. Real per-request identity binding and authorization still require a separate audited server capability. A `sb_secret_*` key requires `NOQERI_WEB_ALLOW_PRIVILEGED_KEY=1` and cannot be combined with `auth_env`. Use elevated credentials only for separately authorized backend-only tasks.

### Firebase Realtime Database, native server

```sh
export FIREBASE_DATABASE_URL='https://YOUR_PROJECT-default-rtdb.firebaseio.com'
export FIREBASE_OAUTH_TOKEN='SHORT_LIVED_GOOGLE_OAUTH_ACCESS_TOKEN'
./build/noqeri-web serve-app examples/native_web_firebase 8080
```

Use `engine: "firebase"`, `dsn_env: "FIREBASE_DATABASE_URL"`, `key_env: "FIREBASE_OAUTH_TOKEN"`, `query items = "GET /items.json"` and `db.execute("POST /items.json", title: form.title)`. Tokens must be obtained and refreshed externally; this connector does **not** implement Google OAuth, Firebase Authentication, Firestore query syntax, or an ID-token exchange.

### Cloud Firestore, native preview

The native `firestore` engine is separate from `firebase` (Realtime Database). Supply a fully qualified document API base and a short-lived **Firebase Auth ID token** (Firestore Security Rules) or an authorized Google OAuth token (IAM); refresh and enforce authorization in the host environment. A fixed environment token is **not** per-request user identity.

```sh
export FIRESTORE_DOCS_URL='https://firestore.googleapis.com/v1/projects/YOUR_PROJECT/databases/(default)/documents'
export FIRESTORE_TOKEN='FIREBASE_ID_TOKEN_OR_GOOGLE_OAUTH_TOKEN'
```

```noqeri
database {
  engine: "firestore"
  dsn_env: "FIRESTORE_DOCS_URL"
  key_env: "FIRESTORE_TOKEN"
}
query notes = "GET /notes"
route "/" {
  main {
    pre { notes }
    form on.submit {
      db.execute("POST /notes", title: form.title)
    } {
      input name: "title" type: "text" {}
      button type: "submit" { "Create" }
    }
  }
}
```

`GET /notes` lists documents; `GET /notes/id` reads one. `POST /notes` creates a document with an autogenerated ID. `PATCH /notes/id` and `DELETE /notes/id` operate conditionally on an existing document, preventing accidental upsert; updates use Firestore field masks. **All native write values are text**, wrapped as `stringValue`. Firestore response JSON is not yet decoded into typed Noqeri records. The Node-managed adapter supports richer typed documents but is a separate runtime. Arbitrary filters, batch writes, transactions, custom document IDs on creation, collection groups and paging are not implemented for the native adapter.

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
python3 tests/native_web_cloud_tls.py ./build/noqeri-web
ctest --test-dir build -R 'noqeri-native-web' --output-on-failure
```

The HTTPS integration test executes real native libcurl requests against a local TLS server, validating certificates, bearer/API headers, tagged Firestore text payloads, and failure isolation. It does **not** establish live Supabase RLS, Firebase Security Rules, Neon connectivity, multiuser authorization, production TLS/load certification or Stage-2 compiler parity. A live integration qualification requires dedicated test projects and credentials, audited policy fixtures, secret rotation and traffic limits.

## Running opt-in LIVE checks

`tests/native_web_live_providers.py` runs read-only queries against **separately provisioned test resources** for Supabase, Cloud Firestore and Neon. It will clearly print `LIVE NOT RUN` for missing credentials; with `NOQERI_LIVE_REQUIRE_ALL=1` missing settings are failures. Use `workflow_dispatch` in `.github/workflows/native-managed-web.yml` and configure the seven `NOQERI_LIVE_*` GitHub Actions secrets for test projects. No real access tokens are checked into Noqeri, and ordinary push CI does **not** claim live provider connectivity.

Native cloud `auth_env` is a fixed server principal, not a user login mechanism; requests under different visitors currently share that identity. A dynamic per-request authz gateway with token verification and RLS/Firestore-rule integration must precede a production deployment. Additional certification gates include Stage-2 typed-NIR lowering, transactions and safe retries, robust request routing, TLS proxy hardening, user-level revocation/MFA, multi-process session consistency, real provider negative authorization policies, fault-injection and load tests, independent security review, and release platform builds.
