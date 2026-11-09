// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <string>
namespace noqeri::network {
struct WirelessResult { int error = 0; std::string value; };
bool wifi_available();
WirelessResult wifi_connected_ssid();
WirelessResult wifi_scan(); // newline-separated SSIDs
int wifi_connect_saved(const std::string& ssid); // saved profiles only; no password in argv
int wifi_disconnect();
bool bluetooth_available();
WirelessResult bluetooth_scan(int timeout_ms); // newline-separated known/discovered devices
std::int64_t bluetooth_connect(const std::string& device_id, int timeout_ms);
int bluetooth_disconnect(std::int64_t handle);
} // namespace noqeri::network
