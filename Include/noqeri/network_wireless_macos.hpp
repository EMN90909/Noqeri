// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noqeri/network_wireless.hpp"
namespace noqeri::network {
bool mac_wifi_available();
WirelessResult mac_wifi_connected_ssid();
WirelessResult mac_wifi_scan();
int mac_wifi_connect_saved(const std::string& ssid);
int mac_wifi_disconnect();
}
