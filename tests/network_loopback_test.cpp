// SPDX-License-Identifier: Apache-2.0
#include "noqeri/network_transport.hpp"
#include "noqeri/network_dns.hpp"
#include <cassert>
#include <string>
#include <thread>
#include <iostream>

using namespace noqeri::network;
int main() {
    auto invalid = tcp_connect("", 80, 100);
    assert(invalid == -2);
    assert(tcp_connect("127.0.0.1", -1, 100) == -2);
    assert(http_status(-100) == -1);
    assert(socket_close(-100) == -1);

    const auto listener = tcp_listen("127.0.0.1", 0, 8);
    assert(listener > 0);
    const int port = socket_local_port(listener);
    assert(port > 0);
    std::thread server([&] {
        const auto peer = tcp_accept(listener, 3000);
        assert(peer > 0);
        const auto read = tcp_receive(peer, 128, 3000);
        assert(read.error == 0 && read.value == "ping");
        assert(tcp_send(peer, "pong", 3000) == 4);
        assert(socket_close(peer) == 0);
    });
    const auto client = tcp_connect("127.0.0.1", port, 3000);
    assert(client > 0);
    assert(tcp_send(client, "ping", 3000) == 4);
    const auto reply = tcp_receive(client, 128, 3000);
    assert(reply.error == 0 && reply.value == "pong");
    assert(socket_close(client) == 0);
    server.join();
    assert(socket_close(listener) == 0);

    const auto udp_receiver = udp_bind("127.0.0.1", 0);
    const auto udp_sender = udp_bind("127.0.0.1", 0);
    assert(udp_receiver > 0 && udp_sender > 0);
    const int udp_port = socket_local_port(udp_receiver);
    assert(udp_port > 0);
    assert(udp_send_to(udp_sender, "127.0.0.1", udp_port, "datagram", 3000) == 8);
    auto packet = udp_receive(udp_receiver, 128, 3000);
    assert(packet.error == 0 && packet.value == "datagram");
    assert(socket_close(udp_receiver) == 0);
    assert(socket_close(udp_sender) == 0);

    if (http_available()) {
        const auto http_listener = tcp_listen("127.0.0.1", 0, 8);
        assert(http_listener > 0);
        const auto http_port = socket_local_port(http_listener);
        std::thread http_server([&] {
            const auto peer = tcp_accept(http_listener, 3000);
            assert(peer > 0);
            const auto request = tcp_receive(peer, 4096, 3000);
            assert(request.error == 0 && request.value.find("GET /health ") != std::string::npos);
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK";
            assert(tcp_send(peer, response, 3000) == static_cast<int>(response.size()));
            assert(socket_close(peer) == 0);
        });
        auto response = http_request("GET", "http://127.0.0.1:" + std::to_string(http_port) + "/health", "", 3000);
        assert(response > 0);
        assert(http_status(response) == 200);
        auto body = http_body(response);
        assert(body.error == 0 && body.value == "OK");
        auto header = http_header(response, "content-type");
        assert(header.error == 0 && header.value == "text/plain");
        assert(http_close(response) == 0);
        http_server.join();
        assert(socket_close(http_listener) == 0);
    }
    std::cout << "native networking loopback tests passed\n";
}
