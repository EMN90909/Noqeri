# Noqeri networking: experimental native integration

This document distinguishes **implemented code** from **verified availability**.
Noqeri v0.1.4 is a developer preview; networking is not production qualified.

## New native networking stack

- `Runtime/native/network_dns.cpp`: OS-backed IPv4/IPv6 resolver.
- `Runtime/native/network_transport.cpp`: TCP connect/listen/accept, bounded send/receive, UDP bind/send/receive, opaque socket handles and explicit close.
- HTTP client on libcurl when available: GET/HEAD/POST/PUT/PATCH/DELETE, HTTP(S), TLS certificate and hostname verification, request deadlines, limited body size, response status/body/header access. Rejects unsupported URL schemes, blocks automatic redirects and caps transfer size.
- `Runtime/native/network_wireless.cpp`: Wi-Fi and Bluetooth adapter interface.
- `Runtime/native/network_wireless_macos.mm`: Apple CoreWLAN adapter.
- `Lib/std/network.noqeri`: beginner-oriented, **reference-interpreter** networking interface. `Lib/std/dns.noqeri`, `Lib/std/wifi.noqeri`, `Lib/std/bluetooth.noqeri` and `Lib/std/http/client.noqeri` also expose some legacy entrypoints.

**Note:** The native x86-64 AOT backend does not automatically link these service implementations. `noqeri build` emits assembly; it does not produce a fully linked network-enabled executable. The interpreter `noqeri run` is the primary route for the new network API.

## Platform support by implementation (hardware verification pending)

| Feature | Linux desktop | Windows desktop | macOS | Android/BSD |
|---|---|---|---|---|
| DNS, TCP, UDP | Native socket code | Native Winsock | Native sockets | Portable socket code, unqualified |
| HTTP/HTTPS client | Native libcurl (optional) | Native libcurl (optional) | Native libcurl (optional) | Native libcurl (optional) |
| Wi-Fi status/scan | NetworkManager `nmcli` | Native WLAN API | CoreWLAN | Unsupported |
| Join saved Wi-Fi network | NetworkManager | Native WLAN profile | CoreWLAN association, dependent on stored credentials | Unsupported |
| Wi-Fi network disconnect | NetworkManager | WLAN API | CoreWLAN | Unsupported |
| Bluetooth discovery and connect/disconnect | BlueZ `bluetoothctl` | Unsupported | Unsupported | Unsupported |
| BLE GATT services/notifications | Not implemented | Not implemented | Not implemented | Not implemented |

Wi-Fi discovery or association may require location permission, system network-management access or user authorization. BlueZ may require pairing, agent configuration and an active Bluetooth controller. Noqeri's Linux Wi-Fi and Bluetooth adapters execute `nmcli` and `bluetoothctl` using bounded, shell-free process spawning. If these tools are absent, the operation returns an explicit negative error code.

**Credential security:** This implementation never inserts a Wi-Fi password into a process command line. `wifi_connect_saved` uses a preexisting OS network profile; passing a nonempty credential to the legacy `wifiConnect` returns `-3` instead of exposing it. Android/iOS app-specific permissions, BLE GATT, production TLS server infrastructure, WebSocket transport and AOT/Stage-2 symbol binding remain separate work.

## Noqeri example (interpreter)

```noqeri
import "Lib/std/network.noqeri"

let ip = resolve_ipv4("127.0.0.1")
print(ip)

let socket = tcp_connect("127.0.0.1", 8080, 3000)
if socket >= 0 {
    tcp_send(socket, "hello", 3000)
    let response = tcp_read(socket, 4096, 3000)
    print(response)
    socket_close(socket)
}

if http_available() {
    let reply = http_get("https://example.org", 5000)
    if reply >= 0 {
        print(http_status(reply))
        print(http_body(reply))
        http_close(reply)
    }
}
```

A real TCP server must be listening at the sample port; the HTTPS example requires network access and a trusted CA bundle. Run Noqeri programs with `noqeri run <source-file>`, not `noqeri build`.

## Verification

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-network-dns-test noqeri-network-loopback-test noqeri-network-wireless-test noqeri-network-abi-test
ctest --test-dir build -R '^noqeri-network-' --output-on-failure
```

The GitHub Actions workflow `.github/workflows/network-dns.yml` runs these checks on Linux, Windows and macOS runners. The loopback test exercises TCP, UDP and HTTP without external services. The ABI test checks DNS execution and that custom embedding hosts do not silently acquire networking privileges. Wireless safety checks run without changing radio state, so passing CI is **not evidence of successful device scanning, pairing or wireless connection on real hardware**.

## Ownership and error contract

- Handles are opaque positive process-local integers. Close every socket and HTTP response handle.
- `-1` generally means an I/O operation failed or timed out; `-2` means invalid arguments; `-3` means an unavailable optional feature or backend.
- Text reads return empty data for either EOF or failure; call `last_error()` immediately to distinguish them. This is provisional until Noqeri's uniform `Result` type is propagated into all backend-native APIs.
- Timeouts are bounded (up to five minutes) and response/body sizes are capped. Calling applications must still set conservative memory, concurrency and credential policies.
- The native embedding ABI returns copied strings; the network runtime owns socket handles and HTTP-response records until explicit close.
- Custom embedding hosts need to explicitly provide network service bindings; they do not automatically inherit the desktop networking services.

## Release qualification gates still open

1. Passing clean Linux, Windows and macOS CI builds plus sanitizer tests.
2. Real Wi-Fi hardware tests on Linux, Windows and macOS, including permission denial and credential flows.
3. Real Bluetooth and BLE GATT discovery, read/write, notification and lifecycle testing on each supported OS.
4. Android JNI, platform permission and lifecycle adapters.
5. Native AOT and Stage-2 compiler symbol binding/ownership correctness for all network calls.
6. End-to-end HTTPS and WSS server/client tests with trust-chain, redirect and malformed input coverage.
7. Production load/security testing (large responses, concurrent close, DNS cancellation, TLS defaults, denial of service).
