// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>

namespace noqeri::network {
// OS resolver; no raw DNS packets, resolver subprocesses, or shell commands.
struct DnsResult {
    std::vector<std::string> addresses;
    std::string error;
    bool ok() const { return error.empty(); }
};
DnsResult resolve_host(const std::string& hostname, bool ipv4_only = false);
} // namespace noqeri::network
