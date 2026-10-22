#!/usr/bin/env bash
set -euo pipefail
test_bin="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 1 \
  -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost' \
  -keyout "$tmp/key.pem" -out "$tmp/cert.pem" >/dev/null 2>&1
"$test_bin" "$tmp/cert.pem" "$tmp/key.pem" "$tmp/cert.pem"
