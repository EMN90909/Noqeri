// SPDX-License-Identifier: Apache-2.0
#include "abi.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

static int32_t sink(void*, const char*, size_t) { return 0; }

int main() {
    const char* source =
        "extern function dnsResolveFirst(host: string, family: int): string\n"
        "print(dnsResolveFirst(\"127.0.0.1\", 4))\n";
    std::ostringstream captured;
    auto* original = std::cout.rdbuf(captured.rdbuf());
    noqeri_abi_error error{};
    int status = noqeri_run_source(source, nullptr, &error);
    std::cout.rdbuf(original);
    if (status != 0) std::cerr << "default ABI returned " << status << "\n";
    assert(status == 0);
    assert(captured.str().find("127.0.0.1") != std::string::npos);

    // Custom embedding hosts must not gain network privileges accidentally.
    noqeri_abi host{};
    host.abi_version = NOQERI_ABI_VERSION;
    host.struct_size = sizeof(host);
    host.write = sink;
    status = noqeri_run_source(source, &host, &error);
    assert(status != 0);

    std::cout << "network interpreter ABI and host isolation passed\n";
}
