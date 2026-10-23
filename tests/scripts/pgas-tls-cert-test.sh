#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "usage: $0 path-to-test-binary" >&2; exit 2; fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
openssl req -x509 -newkey rsa:2048 -sha256 -noenc -keyout "$tmp/ca.key" -out "$tmp/ca.crt" -days 1 -subj "/CN=PGAS-test-CA" >/dev/null 2>&1
openssl req -newkey rsa:2048 -sha256 -noenc -keyout "$tmp/server.key" -out "$tmp/server.csr" -subj "/CN=localhost" >/dev/null 2>&1
printf 'subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n' > "$tmp/server.ext"
openssl x509 -req -in "$tmp/server.csr" -CA "$tmp/ca.crt" -CAkey "$tmp/ca.key" -CAcreateserial -days 1 -out "$tmp/server.crt" -extfile "$tmp/server.ext" >/dev/null 2>&1
openssl req -newkey rsa:2048 -sha256 -noenc -keyout "$tmp/client.key" -out "$tmp/client.csr" -subj "/CN=worker.local" >/dev/null 2>&1
printf 'subjectAltName=DNS:worker.local\nextendedKeyUsage=clientAuth\n' > "$tmp/client.ext"
openssl x509 -req -in "$tmp/client.csr" -CA "$tmp/ca.crt" -CAkey "$tmp/ca.key" -CAcreateserial -days 1 -out "$tmp/client.crt" -extfile "$tmp/client.ext" >/dev/null 2>&1
"$1" "$tmp/ca.crt" "$tmp/server.crt" "$tmp/server.key" "$tmp/client.crt" "$tmp/client.key"
