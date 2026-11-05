// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <string>
namespace noqeri::network {

// Handles are opaque process-local identifiers; -1 represents an error.
// Socket and HTTP-response ownership stays in the runtime until close().
struct TextResult {
    int error = 0; // zero success, negative failure
    std::string value;
};
std::int64_t tcp_connect(const std::string& host, int port, int timeout_ms);
std::int64_t tcp_listen(const std::string& address, int port, int backlog);
std::int64_t tcp_accept(std::int64_t listener, int timeout_ms);
int tcp_send(std::int64_t handle, const std::string& bytes, int timeout_ms);
TextResult tcp_receive(std::int64_t handle, int max_bytes, int timeout_ms);
std::int64_t udp_bind(const std::string& address, int port);
int udp_send_to(std::int64_t handle, const std::string& host, int port,
                const std::string& bytes, int timeout_ms);
TextResult udp_receive(std::int64_t handle, int max_bytes, int timeout_ms);
int socket_close(std::int64_t handle);
int socket_local_port(std::int64_t handle);

std::int64_t http_request(const std::string& method, const std::string& url,
                          const std::string& body, int timeout_ms);
int http_status(std::int64_t handle);
TextResult http_body(std::int64_t handle);
TextResult http_header(std::int64_t handle, const std::string& name);
int http_close(std::int64_t handle);
bool http_available();
} // namespace noqeri::network
