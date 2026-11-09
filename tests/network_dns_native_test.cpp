// SPDX-License-Identifier: Apache-2.0
#include "noqeri/network_dns.hpp"
#include <cassert>
#include <iostream>

int main() {
    auto bad = noqeri::network::resolve_host("");
    assert(!bad.ok());
    auto invalid = noqeri::network::resolve_host(std::string(254, 'a'));
    assert(!invalid.ok());
    auto v4 = noqeri::network::resolve_host("127.0.0.1", true);
    assert(v4.ok());
    assert(v4.addresses.size() == 1);
    assert(v4.addresses[0] == "127.0.0.1");
    auto loopback = noqeri::network::resolve_host("::1", false);
    assert(loopback.ok());
    std::cout << "network DNS native smoke test passed\n";
}
