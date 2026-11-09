// SPDX-License-Identifier: Apache-2.0
#include "noqeri/network_wireless.hpp"
#include <cassert>
#include <iostream>
using namespace noqeri::network;
int main() {
    // Never change radio state in an unattended CI runner.
    (void)wifi_available();
    (void)bluetooth_available();
    assert(wifi_connect_saved("") == -2
#ifndef __linux__
           || wifi_connect_saved("") == -3
#endif
    );
#ifdef __linux__
    assert(bluetooth_connect("invalid-device", 2000) == -2);
    assert(bluetooth_scan(1).error == -2);
    assert(bluetooth_disconnect(-1) == -1);
#else
    assert(bluetooth_connect("invalid-device", 2000) == -3);
    assert(bluetooth_scan(1).error == -3);
    assert(bluetooth_disconnect(-1) == -3);
#endif
    std::cout << "wireless adapter no-hardware safety tests passed\n";
}
