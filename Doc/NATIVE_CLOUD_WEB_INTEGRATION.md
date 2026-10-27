# Native cloud database and reactive web preview

Scope: C++ bootstrap, not full Stage-2 or production-certified web/auth.

## Providers
Existing Noqeri native DB engines: NoeSQL, SQLite, PostgreSQL, DuckDB, MySQL/MariaDB. PostgreSQL-compatible cloud vendors such as Neon and Supabase direct Postgres use the existing postgresql driver with a TLS connection string (check server certificate verification). NeoBase is a database copilot rather than a separate storage engine: connect to its underlying supported SQL database.

Supabase uses the PostgREST Data API at the official project hostname. Configure the database block with engine: "supabase", endpoint_env: "SUPABASE_PROJECT_URL", key_env: "SUPABASE_PUBLISHABLE_KEY", and optionally token_env: "SUPABASE_USER_JWT". Use query messages = "GET messages?select=id,name&limit=20" for a read-only view, or db.execute("POST messages", name: form.name) for a bounded JSON-string-valued write. The published key does not confer user identity: you must enforce appropriate RLS and access policies. A single process environment token is NOT per-user auth; do not claim this implements Supabase user sessions.

Firebase uses the Realtime Database REST API, NOT Firestore. Configure engine: "firebase", endpoint_env: "FIREBASE_RTDB_URL", token_env: "FIREBASE_SERVER_OAUTH_TOKEN". Example read: query notes = "GET notes". Example write: db.execute("POST notes", name: form.name). An OAuth2 token must be refreshed externally. Never publish service credentials or use privileged tokens without your own authorization.

Cloud operations require optional libcurl, HTTPS certificate validation, no redirects, bounded request/response, static author-controlled API resource, environment-only credentials. Reads render JSON as HTML-escaped text; values written are JSON strings, not typed nested documents. Bundled standalone C++ generated apps currently do not link optional libcurl cloud drivers. Unit tests are offline; real live cloud integration remains unqualified.

## Form validation and progressive enhancement
Server actions now accept validate(field, minBytes, maxBytes), validateEmail(field), validateNumber(field, min, max). HTML input attrs also accept required, minlength, maxlength, min, max and autocomplete. These are basic checks, not full schema validation. CSRF and Origin checks remain server-side.

Set NOQERI_WEB_REACTIVE=1 to enable an optional same-origin Fetch client. It intercepts only framework-owned POST forms and re-renders returned server HTML after redirects. Without this flag, Noqeri keeps the original HTML/CSS-only behavior. No general client-side reactivity/signal compiler or live WebSocket app subscription is claimed.

For private machine-to-machine HTTP endpoints, configure NOQERI_WEB_BEARER_TOKEN to a high-entropy 32..512 byte string. This is mutually exclusive with NOQERI_WEB_BASIC_USER/PASSWORD. It is not full account authentication, OAuth or authorization. Sessions remain in memory, with CSRF rotation, TTL, and HttpOnly SameSite cookies. State files now use exclusive mode-0600 creation, file sync, atomic rename and directory sync on POSIX. They are a small local store, not distributed durable sessions.

Verification:
  cmake -S . -B build -DBUILD_TESTING=ON
  cmake --build build --target noqeri-web noqeri-web-cloud-test
  ctest --test-dir build -R noqeri-web-cloud --output-on-failure
  python3 tests/native_web_reactive_security.py ./build/noqeri-web

References: https://supabase.com/docs/guides/api and https://firebase.google.com/docs/database/rest/start and https://firebase.google.com/docs/database/rest/auth
