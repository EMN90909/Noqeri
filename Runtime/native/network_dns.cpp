// SPDX-License-Identifier: Apache-2.0
#include "noqeri/network_dns.hpp"
#include <cstring>
#include <set>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace noqeri::network {
DnsResult resolve_host(const std::string& hostname, bool ipv4_only) {
    DnsResult out;
    if (hostname.empty() || hostname.size() > 253 || hostname.find('\0') != std::string::npos) {
        out.error = "invalid hostname";
        return out;
    }
#ifdef _WIN32
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) {
        out.error = "Winsock initialization failed";
        return out;
    }
#endif
    addrinfo hints{};
    hints.ai_family = ipv4_only ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* head = nullptr;
    int code = getaddrinfo(hostname.c_str(), nullptr, &hints, &head);
    if (code != 0) {
#ifdef _WIN32
        out.error = gai_strerrorA(code);
        WSACleanup();
#else
        out.error = gai_strerror(code);
#endif
        return out;
    }
    std::set<std::string> unique;
    for (addrinfo* p = head; p; p = p->ai_next) {
        char buffer[INET6_ADDRSTRLEN]{};
        const void* address = nullptr;
        if (p->ai_family == AF_INET) {
            address = &reinterpret_cast<const sockaddr_in*>(p->ai_addr)->sin_addr;
        } else if (p->ai_family == AF_INET6) {
            address = &reinterpret_cast<const sockaddr_in6*>(p->ai_addr)->sin6_addr;
        }
        if (address && inet_ntop(p->ai_family, address, buffer, sizeof(buffer))) {
            unique.emplace(buffer);
        }
    }
    freeaddrinfo(head);
#ifdef _WIN32
    WSACleanup();
#endif
    out.addresses.assign(unique.begin(), unique.end());
    if (out.addresses.empty()) out.error = "no addresses found";
    return out;
}
} // namespace noqeri::network
