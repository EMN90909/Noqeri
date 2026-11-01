// SPDX-License-Identifier: Apache-2.0
// Linux desktop adapters use NetworkManager and BlueZ command-line clients,
// invoked with posix_spawnp (never a shell). Commands are bounded and return
// explicit unsupported errors when an adapter is not available. Passwords
// are deliberately NOT passed via argv or shell strings.
#include "noqeri/network_wireless.hpp"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace noqeri::network {
namespace {
std::mutex bt_lock;
std::map<std::int64_t, std::string> bt_handles;
std::atomic<std::int64_t> next_bt_handle{1};
#ifdef __linux__
struct CmdResult { int error = 0; std::string output; };
CmdResult command(const std::vector<std::string>& args, int timeout_ms = 5000) {
    if (args.empty() || timeout_ms < 100 || timeout_ms > 300000) return {-2, {}};
    int fd[2];
    if (pipe(fd) != 0) return {-1, {}};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fd[0]);
    posix_spawn_file_actions_addclose(&actions, fd[1]);
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    pid_t child = 0;
    const int rc = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fd[1]);
    if (rc != 0) { close(fd[0]); return {rc == ENOENT ? -3 : -1, {}}; }
    int flags = fcntl(fd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(fd[0], F_SETFL, flags | O_NONBLOCK);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string output;
    int status = 0;
    bool exited = false, eof = false;
    while (std::chrono::steady_clock::now() < until) {
        char buffer[2048];
        for (;;) {
            ssize_t n = read(fd[0], buffer, sizeof(buffer));
            if (n > 0) {
                if (output.size() + static_cast<std::size_t>(n) > 65536) {
                    kill(child, SIGKILL); waitpid(child, &status, 0);
                    close(fd[0]); return {-2, {}};
                }
                output.append(buffer, static_cast<std::size_t>(n));
            } else {
                if (n == 0) eof = true;
                break;
            }
        }
        if (!exited) {
            pid_t r = waitpid(child, &status, WNOHANG);
            if (r == child) exited = true;
            else if (r < 0) { close(fd[0]); return {-1, {}}; }
        }
        if (exited && eof) { close(fd[0]); return {WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1, output}; }
        pollfd pfd{fd[0], POLLIN | POLLHUP, 0};
        poll(&pfd, 1, 20);
    }
    if (!exited) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    close(fd[0]);
    return {-1, {}}; // timed out
}
std::string first_wifi_device() {
    auto r = command({"nmcli", "-t", "--escape", "no", "-f", "DEVICE,TYPE", "device", "status"});
    if (r.error != 0) return {};
    std::size_t begin = 0;
    while (begin < r.output.size()) {
        auto end = r.output.find('\n', begin);
        auto line = r.output.substr(begin, end == std::string::npos ? end : end - begin);
        auto pos = line.rfind(":wifi");
        if (pos != std::string::npos && pos + 5 == line.size()) return line.substr(0, pos);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}
bool mac_valid(const std::string& id) {
    if (id.size() != 17) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i % 3 == 2) { if (id[i] != ':') return false; }
        else if (!((id[i] >= '0' && id[i] <= '9') ||
                   (id[i] >= 'A' && id[i] <= 'F') ||
                   (id[i] >= 'a' && id[i] <= 'f'))) return false;
    }
    return true;
}
#endif
} // namespace

bool wifi_available() {
#ifdef __linux__
    return !first_wifi_device().empty();
#else
    return false; // adapter not linked on this OS
#endif
}
WirelessResult wifi_connected_ssid() {
#ifdef __linux__
    auto r = command({"nmcli", "-t", "--escape", "no", "-f", "IN-USE,SSID", "device", "wifi", "list", "--rescan", "no"});
    if (r.error != 0) return {r.error, {}};
    std::size_t begin = 0;
    while (begin < r.output.size()) {
        auto end = r.output.find('\n', begin);
        auto line = r.output.substr(begin, end == std::string::npos ? end : end - begin);
        if (line.compare(0, 2, "*:") == 0) return {0, line.substr(2)};
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {0, {}};
#else
    return {-3, {}};
#endif
}
WirelessResult wifi_scan() {
#ifdef __linux__
    auto r = command({"nmcli", "-t", "--escape", "no", "-f", "SSID", "device", "wifi", "list", "--rescan", "yes"}, 15000);
    return {r.error, r.output};
#else
    return {-3, {}};
#endif
}
int wifi_connect_saved(const std::string& ssid) {
#ifdef __linux__
    if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != std::string::npos) return -2;
    return command({"nmcli", "connection", "up", "id", ssid}, 30000).error;
#else
    (void)ssid; return -3;
#endif
}
int wifi_disconnect() {
#ifdef __linux__
    auto device = first_wifi_device();
    if (device.empty()) return -3;
    return command({"nmcli", "device", "disconnect", device}, 15000).error;
#else
    return -3;
#endif
}
bool bluetooth_available() {
#ifdef __linux__
    auto r = command({"bluetoothctl", "list"});
    return r.error == 0 && r.output.find("Controller ") != std::string::npos;
#else
    return false;
#endif
}
WirelessResult bluetooth_scan(int timeout_ms) {
#ifdef __linux__
    if (timeout_ms < 1000 || timeout_ms > 30000) return {-2, {}};
    if (!bluetooth_available()) return {-3, {}};
    auto r = command({"bluetoothctl", "--timeout", std::to_string((timeout_ms + 999) / 1000), "scan", "on"}, timeout_ms + 1500);
    if (r.error != 0) return {r.error, {}};
    auto devices = command({"bluetoothctl", "devices"});
    return {devices.error, devices.output};
#else
    (void)timeout_ms; return {-3, {}};
#endif
}
std::int64_t bluetooth_connect(const std::string& device_id, int timeout_ms) {
#ifdef __linux__
    if (!mac_valid(device_id) || timeout_ms < 1000 || timeout_ms > 30000) return -2;
    if (!bluetooth_available()) return -3;
    auto r = command({"bluetoothctl", "--timeout", std::to_string((timeout_ms+999)/1000), "connect", device_id}, timeout_ms+1500);
    if (r.error != 0 || r.output.find("Connection successful") == std::string::npos) return -1;
    std::lock_guard<std::mutex> guard(bt_lock);
    auto handle = next_bt_handle++;
    bt_handles.emplace(handle, device_id);
    return handle;
#else
    (void)device_id; (void)timeout_ms; return -3;
#endif
}
int bluetooth_disconnect(std::int64_t handle) {
#ifdef __linux__
    std::string id;
    {
        std::lock_guard<std::mutex> guard(bt_lock);
        auto it = bt_handles.find(handle);
        if (it == bt_handles.end()) return -1;
        id = it->second;
    }
    auto result = command({"bluetoothctl", "disconnect", id}, 10000);
    if (result.error == 0) {
        std::lock_guard<std::mutex> guard(bt_lock);
        bt_handles.erase(handle);
    }
    return result.error;
#else
    (void)handle; return -3;
#endif
}
} // namespace noqeri::network
