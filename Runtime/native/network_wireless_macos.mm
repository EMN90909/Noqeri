// SPDX-License-Identifier: Apache-2.0
// CoreWLAN is supported only on macOS desktop. Scans and association can be
// restricted by Location Services, sandboxing, and OS privacy prompts.
#include "noqeri/network_wireless_macos.hpp"
#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>
namespace noqeri::network {
namespace {
CWInterface* interface() { return [[CWWiFiClient sharedWiFiClient] interface]; }
std::string text(NSString* value) {
    return value && [value UTF8String] ? std::string([value UTF8String]) : std::string{};
}
}
bool mac_wifi_available() {
    @autoreleasepool { return interface() != nil; }
}
WirelessResult mac_wifi_connected_ssid() {
    @autoreleasepool {
        CWInterface* device = interface();
        return device ? WirelessResult{0, text([device ssid])} : WirelessResult{-3, {}};
    }
}
WirelessResult mac_wifi_scan() {
    @autoreleasepool {
        CWInterface* device = interface();
        if (!device) return {-3, {}};
        NSError* error = nil;
        NSSet<CWNetwork*>* networks = [device scanForNetworksWithName:nil error:&error];
        if (!networks || error) return {-1, {}};
        std::string names;
        for (CWNetwork* network in networks) {
            auto name = text([network ssid]);
            if (name.empty()) continue;
            for (char& ch : name) if (ch == '\r' || ch == '\n') ch = ' ';
            names += name + "\n";
        }
        return {0, names};
    }
}
int mac_wifi_connect_saved(const std::string& ssid) {
    if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != std::string::npos) return -2;
    @autoreleasepool {
        CWInterface* device = interface();
        if (!device) return -3;
        NSError* error = nil;
        NSSet<CWNetwork*>* networks = [device scanForNetworksWithName:nil error:&error];
        if (!networks || error) return -1;
        NSString* name = [NSString stringWithUTF8String:ssid.c_str()];
        if (!name) return -2;
        for (CWNetwork* network in networks)
            if ([[network ssid] isEqualToString:name]) {
                return [device associateToNetwork:network password:nil error:&error] ? 0 : -1;
            }
        return -1;
    }
}
int mac_wifi_disconnect() {
    @autoreleasepool {
        CWInterface* device = interface();
        if (!device) return -3;
        [device disassociate];
        return 0;
    }
}
}
