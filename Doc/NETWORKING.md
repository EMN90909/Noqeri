# Noqeri native networking roadmap

## Existing interfaces
- `Lib/std/tcp.noqeri`: client/server sockets and read/write.
- `Lib/std/udp.noqeri`: datagrams.
- `Lib/std/http/client.noqeri`: HTTP verbs, response status, headers and body.
- `Lib/std/http/server.noqeri`: server helpers.
- `Lib/std/websocket.noqeri`: experimental WebSocket interface; production transport is not qualified.

These APIs are not necessarily wired into every compiler/backend. Presence of an `extern` declaration is **not proof of a working native implementation**.

## This change
1. Add a dependency-free C++17 DNS resolver at `Include/noqeri/network_dns.hpp` and `Runtime/native/network_dns.cpp`, based on the host OS's `getaddrinfo`; returns IPv4 and IPv6 string addresses with errors.
2. Add standalone deterministic DNS smoke tests using loopback literals (no internet access required).
3. Declare Noqeri `std.dns`, `std.wifi`, and `std.bluetooth` **experimental API contracts**. These bindings are **not yet linked** to Noqeri native executables. Wi-Fi and Bluetooth scanning and connection are **not yet implemented**.

## Standalone DNS verification

Linux/macOS:
```sh
c++ -std=c++17 -IInclude Runtime/native/network_dns.cpp tests/network_dns_native_test.cpp -o /tmp/noqeri-dns-test
/tmp/noqeri-dns-test
```
Windows (MSVC developer prompt):
```bat
cl /std:c++17 /EHsc /IInclude Runtime\native\network_dns.cpp tests\network_dns_native_test.cpp /link Ws2_32.lib
network_dns_native_test.exe
```

## Follow-up integration work (required before claiming native feature support)
- Bind `dnsResolveFirst` to the C++ resolver through the existing compiler/runtime external-call ABI, including ownership/lifetime of returned Noqeri strings.
- Add first-class `Result` errors, async DNS cancellation/timeouts, and resolver tests for dual-stack hosts.
- Implement Wi-Fi on Linux (NetworkManager or wpa_supplicant), Windows (WLAN API), macOS (CoreWLAN where permitted), and Android (WifiManager with permissions). iOS and sandboxed platforms restrict scans and connection management.
- Implement Bluetooth Low Energy using BlueZ, Windows Bluetooth APIs, CoreBluetooth, and Android BluetoothLe APIs; ask permissions and never silently scan/connect.
- Add socket timeouts, TLS certificate verification, thread-safe lifecycle handling, fuzz/security tests and platform CI before marking production ready.

Transporting data **over Wi-Fi** normally uses TCP/UDP/HTTP through the OS's existing network interface; radio scanning and joining networks require additional device-management APIs.
