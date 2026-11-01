// SPDX-License-Identifier: Apache-2.0
#include "noqeri/network_transport.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
using SockLen = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
static void close_native(Socket s) { closesocket(s); }
static int last_socket_error() { return WSAGetLastError(); }
static bool would_block(int err) { return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using Socket = int;
using SockLen = socklen_t;
constexpr Socket invalid_socket = -1;
static void close_native(Socket s) { ::close(s); }
static int last_socket_error() { return errno; }
static bool would_block(int err) { return err == EINPROGRESS || err == EWOULDBLOCK; }
#endif
#ifdef NOQERI_NETWORK_CURL
#include <curl/curl.h>
#endif

namespace noqeri::network {
namespace {
struct Entry {
    explicit Entry(Socket s) : fd(s) {}
    Socket fd;
    std::mutex lock;
};
std::mutex sockets_lock, responses_lock;
std::map<std::int64_t, std::shared_ptr<Entry>> sockets;
std::atomic<std::int64_t> next_handle{1};
std::once_flag init_flag;
bool net_ready = false;
struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};
std::map<std::int64_t, HttpResponse> responses;
bool ready() {
    std::call_once(init_flag, [] {
#ifdef _WIN32
        WSADATA wd{};
        net_ready = WSAStartup(MAKEWORD(2, 2), &wd) == 0;
#else
        net_ready = true;
#endif
    });
    return net_ready;
}
bool valid_port(int p) { return p >= 0 && p <= 65535; }
bool timeout_valid(int ms) { return ms > 0 && ms <= 300000; }
bool set_blocking(Socket s, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
#endif
}
bool wait_ready(Socket s, bool writing, int timeout_ms) {
    if (!timeout_valid(timeout_ms)) return false;
    fd_set set;
    FD_ZERO(&set); FD_SET(s, &set);
    timeval t{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    // On Windows nfds is ignored, on Unix it is one more than the fd.
    int ret = writing ? select(static_cast<int>(s) + 1, nullptr, &set, nullptr, &t)
                      : select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &t);
    return ret > 0;
}
std::int64_t track(Socket s) {
    if (s == invalid_socket) return -1;
    std::lock_guard<std::mutex> guard(sockets_lock);
    const auto id = next_handle++;
    sockets.emplace(id, std::make_shared<Entry>(s));
    return id;
}
std::shared_ptr<Entry> lookup(std::int64_t id) {
    std::lock_guard<std::mutex> guard(sockets_lock);
    auto it = sockets.find(id);
    return it == sockets.end() ? nullptr : it->second;
}
addrinfo* addresses(const std::string& host, int port, int kind, bool passive) {
    if (!ready() || !valid_port(port) || host.find('\0') != std::string::npos) return nullptr;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = kind;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    addrinfo* result = nullptr;
    auto service = std::to_string(port);
    if (getaddrinfo(host.empty() && passive ? nullptr : host.c_str(),
                    service.c_str(), &hints, &result) != 0) return nullptr;
    return result;
}
#ifdef NOQERI_NETWORK_CURL
size_t on_body(char* data, size_t a, size_t b, void* ptr) {
    auto* response = static_cast<HttpResponse*>(ptr);
    const size_t n = a * b;
    if (n > 8 * 1024 * 1024 || response->body.size() > 8 * 1024 * 1024 - n) return 0;
    response->body.append(data, n);
    return n;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
size_t on_header(char* data, size_t a, size_t b, void* ptr) {
    auto* response = static_cast<HttpResponse*>(ptr);
    const size_t n = a * b;
    if (n > 16384) return 0;
    std::string line(data, n);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = lower(line.substr(0, colon));
        std::size_t start = colon + 1;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        auto end = line.find_first_of("\r\n", start);
        response->headers[key] = line.substr(start, end == std::string::npos ? end : end - start);
    }
    return n;
}
#endif
} // namespace

std::int64_t tcp_connect(const std::string& host, int port, int timeout_ms) {
    if (host.empty() || !valid_port(port) || port == 0 || !timeout_valid(timeout_ms)) return -2;
    addrinfo* list = addresses(host, port, SOCK_STREAM, false);
    if (!list) return -1;
    Socket opened = invalid_socket;
    for (auto p = list; p; p = p->ai_next) {
        Socket fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == invalid_socket) continue;
        if (!set_blocking(fd, false)) { close_native(fd); continue; }
        int rc = ::connect(fd, p->ai_addr, static_cast<SockLen>(p->ai_addrlen));
        if (rc != 0 && !would_block(last_socket_error())) { close_native(fd); continue; }
        if (rc != 0 && !wait_ready(fd, true, timeout_ms)) { close_native(fd); continue; }
        int so_error = 0; SockLen length = static_cast<SockLen>(sizeof(so_error));
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &length) != 0 || so_error != 0) {
            close_native(fd); continue;
        }
        opened = fd; break;
    }
    freeaddrinfo(list);
    return track(opened);
}
std::int64_t tcp_listen(const std::string& address, int port, int backlog) {
    if (!valid_port(port) || backlog < 1 || backlog > 1024) return -2;
    addrinfo* list = addresses(address, port, SOCK_STREAM, true);
    if (!list) return -1;
    Socket opened = invalid_socket;
    for (auto p = list; p; p = p->ai_next) {
        Socket fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == invalid_socket) continue;
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        if (bind(fd, p->ai_addr, static_cast<SockLen>(p->ai_addrlen)) == 0 &&
            listen(fd, backlog) == 0 && set_blocking(fd, false)) { opened = fd; break; }
        close_native(fd);
    }
    freeaddrinfo(list);
    return track(opened);
}
std::int64_t tcp_accept(std::int64_t listener, int timeout_ms) {
    if (!timeout_valid(timeout_ms)) return -2;
    auto e = lookup(listener);
    if (!e) return -1;
    std::lock_guard<std::mutex> guard(e->lock);
    if (e->fd == invalid_socket || !wait_ready(e->fd, false, timeout_ms)) return -1;
    Socket accepted = accept(e->fd, nullptr, nullptr);
    if (accepted == invalid_socket) return -1;
    if (!set_blocking(accepted, false)) { close_native(accepted); return -1; }
    return track(accepted);
}
int tcp_send(std::int64_t handle, const std::string& bytes, int timeout_ms) {
    if (!timeout_valid(timeout_ms) || bytes.size() > 8 * 1024 * 1024) return -2;
    auto e = lookup(handle);
    if (!e) return -1;
    std::lock_guard<std::mutex> guard(e->lock);
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        if (!wait_ready(e->fd, true, timeout_ms)) return -1;
        const std::size_t part = std::min<std::size_t>(bytes.size() - sent, 65536);
        int n = send(e->fd, bytes.data() + sent, static_cast<int>(part), 0);
        if (n <= 0) return -1;
        sent += static_cast<std::size_t>(n);
    }
    return static_cast<int>(sent);
}
TextResult tcp_receive(std::int64_t handle, int max_bytes, int timeout_ms) {
    if (max_bytes <= 0 || max_bytes > 1048576 || !timeout_valid(timeout_ms)) return {-2, {}};
    auto e = lookup(handle);
    if (!e) return {-1, {}};
    std::lock_guard<std::mutex> guard(e->lock);
    if (!wait_ready(e->fd, false, timeout_ms)) return {-1, {}};
    std::string result(static_cast<std::size_t>(max_bytes), '\0');
    int n = recv(e->fd, &result[0], max_bytes, 0);
    if (n < 0) return {-1, {}};
    result.resize(static_cast<std::size_t>(n));
    return {0, result};
}
std::int64_t udp_bind(const std::string& address, int port) {
    if (!valid_port(port)) return -2;
    addrinfo* list = addresses(address, port, SOCK_DGRAM, true);
    if (!list) return -1;
    Socket opened = invalid_socket;
    for (auto p = list; p; p = p->ai_next) {
        Socket fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == invalid_socket) continue;
        if (bind(fd, p->ai_addr, static_cast<SockLen>(p->ai_addrlen)) == 0 &&
            set_blocking(fd, false)) { opened = fd; break; }
        close_native(fd);
    }
    freeaddrinfo(list);
    return track(opened);
}
int udp_send_to(std::int64_t handle, const std::string& host, int port,
                const std::string& bytes, int timeout_ms) {
    if (host.empty() || !valid_port(port) || port == 0 || bytes.size() > 65507 || !timeout_valid(timeout_ms)) return -2;
    auto e = lookup(handle);
    if (!e) return -1;
    addrinfo* list = addresses(host, port, SOCK_DGRAM, false);
    if (!list) return -1;
    std::lock_guard<std::mutex> guard(e->lock);
    int result = -1;
    for (auto p = list; p; p = p->ai_next) {
        if (p->ai_family != AF_INET && p->ai_family != AF_INET6) continue;
        if (!wait_ready(e->fd, true, timeout_ms)) break;
        int n = sendto(e->fd, bytes.data(), static_cast<int>(bytes.size()), 0,
                       p->ai_addr, static_cast<SockLen>(p->ai_addrlen));
        if (n >= 0) { result = n; break; }
    }
    freeaddrinfo(list);
    return result;
}
TextResult udp_receive(std::int64_t handle, int max_bytes, int timeout_ms) {
    return tcp_receive(handle, max_bytes, timeout_ms); // datagram payload; sender metadata not yet returned
}
int socket_close(std::int64_t handle) {
    std::shared_ptr<Entry> e;
    {
        std::lock_guard<std::mutex> guard(sockets_lock);
        auto it = sockets.find(handle);
        if (it == sockets.end()) return -1;
        e = it->second;
        sockets.erase(it);
    }
    std::lock_guard<std::mutex> guard(e->lock);
    if (e->fd != invalid_socket) { close_native(e->fd); e->fd = invalid_socket; }
    return 0;
}
std::int64_t http_request(const std::string& method, const std::string& url,
                          const std::string& body, int timeout_ms) {
#ifndef NOQERI_NETWORK_CURL
    (void)method; (void)url; (void)body; (void)timeout_ms;
    return -3; // libcurl optional and unavailable
#else
    if (!timeout_valid(timeout_ms) || url.size() > 8192 || body.size() > 8 * 1024 * 1024 ||
        (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)) return -2;
    if (method != "GET" && method != "HEAD" && method != "POST" &&
        method != "PUT" && method != "PATCH" && method != "DELETE") return -2;
    static std::once_flag curl_flag;
    std::call_once(curl_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    HttpResponse response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // no unreviewed redirects
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout_ms));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Noqeri-Network/0.1");
    if (method == "HEAD") curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK || status <= 0) return -1;
    response.status = static_cast<int>(status);
    const auto id = next_handle++;
    std::lock_guard<std::mutex> guard(responses_lock);
    responses.emplace(id, std::move(response));
    return id;
#endif
}
int http_status(std::int64_t handle) {
    std::lock_guard<std::mutex> guard(responses_lock);
    auto it = responses.find(handle);
    return it == responses.end() ? -1 : it->second.status;
}
TextResult http_body(std::int64_t handle) {
    std::lock_guard<std::mutex> guard(responses_lock);
    auto it = responses.find(handle);
    return it == responses.end() ? TextResult{-1, {}} : TextResult{0, it->second.body};
}
TextResult http_header(std::int64_t handle, const std::string& name) {
    std::lock_guard<std::mutex> guard(responses_lock);
    auto it = responses.find(handle);
    if (it == responses.end()) return {-1, {}};
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    auto found = it->second.headers.find(key);
    return found == it->second.headers.end() ? TextResult{0, {}} : TextResult{0, found->second};
}
int http_close(std::int64_t handle) {
    std::lock_guard<std::mutex> guard(responses_lock);
    return responses.erase(handle) == 0 ? -1 : 0;
}
bool http_available() {
#ifdef NOQERI_NETWORK_CURL
    return true;
#else
    return false;
#endif
}
} // namespace noqeri::network
