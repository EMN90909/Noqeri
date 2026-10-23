#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root, using two different container network namespaces.
# This qualifies separated processes and network namespaces, NOT physical hosts.
test "$(id -u)" -ge 0 || true
tmp="$(mktemp -d)"
network="pgas-namespace-$$"
cleanup() {
  docker rm -f "pgas-node-$$" >/dev/null 2>&1 || true
  docker network rm "$network" >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -keyout "$tmp/ca.key" -out "$tmp/ca.crt" -days 1 -subj "/CN=PGAS-container-test-CA" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -keyout "$tmp/server.key" -out "$tmp/server.csr" -subj "/CN=pgas-node" >/dev/null 2>&1
printf 'subjectAltName=DNS:pgas-node\nextendedKeyUsage=serverAuth\n' > "$tmp/server.ext"
openssl x509 -req -in "$tmp/server.csr" -CA "$tmp/ca.crt" -CAkey "$tmp/ca.key" -CAcreateserial -out "$tmp/server.crt" -days 1 -extfile "$tmp/server.ext" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -keyout "$tmp/client.key" -out "$tmp/client.csr" -subj "/CN=worker.local" >/dev/null 2>&1
printf 'subjectAltName=DNS:worker.local\nextendedKeyUsage=clientAuth\n' > "$tmp/client.ext"
openssl x509 -req -in "$tmp/client.csr" -CA "$tmp/ca.crt" -CAkey "$tmp/ca.key" -CAcreateserial -out "$tmp/client.crt" -days 1 -extfile "$tmp/client.ext" >/dev/null 2>&1
chmod 644 "$tmp/"*.crt
cp "$1" "$tmp/pgas_remote_node"
cp "$2" "$tmp/pgas_remote_client"
cp tests/docker/pgas-test.Dockerfile "$tmp/Dockerfile"
docker build -q -t "noqeri-pgas-ns:ci" "$tmp" >/dev/null
docker network create "$network" >/dev/null
docker run -d --name "pgas-node-$$" --network "$network" --network-alias pgas-node \
  -v "$tmp:/certs:ro" "noqeri-pgas-ns:ci" \
  /usr/local/bin/pgas_remote_node 0.0.0.0 9943 /certs/ca.crt /certs/server.crt /certs/server.key worker.local >/dev/null
for n in $(seq 1 40); do
  if docker run --rm --network "$network" -v "$tmp:/certs:ro" "noqeri-pgas-ns:ci" \
     /usr/local/bin/pgas_remote_client pgas-node 9943 /certs/ca.crt /certs/client.crt /certs/client.key pgas-node unused; then
      exit 0
  fi
  if ! docker inspect "pgas-node-$$" --format '{{.State.Running}}' | grep -q true; then
    docker logs "pgas-node-$$"; exit 1
  fi
  sleep 1
done
docker logs "pgas-node-$$"
exit 1
