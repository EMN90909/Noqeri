#pragma once
// Bounded RFC 6455 frame-layer foundation. No sockets, handshake, TLS or JS.
// Rejects fragmentation until a streaming reassembler is implemented.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace noqeri_websocket {

constexpr std::size_t max_frame_bytes = 1024 * 1024;

inline bool valid_close_code(std::uint16_t code) {
    return code >= 1000 && code < 5000 &&
           code != 1004 && code != 1005 && code != 1006 && code != 1015 &&
           !(code >= 1016 && code < 3000);
}
inline bool valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto a = static_cast<unsigned char>(text[i]);
        if (a < 128) {
            ++i; continue;
        }
        std::size_t n = 0;
        std::uint32_t cp = 0, min = 0;
        if ((a & 0xe0) == 0xc0) { n = 2; cp = a & 0x1f; min = 128; }
        else if ((a & 0xf0) == 0xe0) { n = 3; cp = a & 0x0f; min = 2048; }
        else if ((a & 0xf8) == 0xf0) { n = 4; cp = a & 0x07; min = 65536; }
        else return false;
        if (n > text.size() - i) return false;
        for (std::size_t j = 1; j < n; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += n;
    }
    return true;
}
enum class Status { ok, incomplete, invalid };
struct Frame {
    std::uint8_t opcode = 0;
    bool fin = true;
    std::string payload;
};
struct Decode {
    Status status = Status::invalid;
    Frame frame;
    std::size_t consumed = 0;
};
inline Decode decode(const std::uint8_t* data, std::size_t size, bool from_client) {
    if (!data) return {Status::invalid, {}, 0};
    if (size < 2) return {Status::incomplete, {}, 0};
    const std::uint8_t first = data[0], second = data[1];
    const bool fin = (first & 0x80) != 0;
    if (first & 0x70) return {Status::invalid, {}, 0}; // extensions/Rsv require negotiation
    const std::uint8_t opcode = first & 0x0f;
    const bool control = opcode >= 8;
    if (opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10)
        return {Status::invalid, {}, 0}; // no continuation/reassembly in bounded codec
    if (!fin) return {Status::invalid, {}, 0};
    const bool masked = (second & 0x80) != 0;
    if (masked != from_client) return {Status::invalid, {}, 0};
    std::uint64_t length = second & 0x7f;
    std::size_t offset = 2;
    if (length == 126) {
        if (size < 4) return {Status::incomplete, {}, 0};
        length = (std::uint64_t(data[2]) << 8) | data[3];
        if (length < 126) return {Status::invalid, {}, 0}; // non-minimal encoding
        offset = 4;
    } else if (length == 127) {
        if (size < 10) return {Status::incomplete, {}, 0};
        if (data[2] & 0x80) return {Status::invalid, {}, 0};
        length = 0;
        for (int i = 2; i < 10; ++i) length = (length << 8) | data[i];
        if (length < 65536) return {Status::invalid, {}, 0};
        offset = 10;
    }
    if (length > max_frame_bytes || (control && length > 125))
        return {Status::invalid, {}, 0};
    if (masked) {
        if (size < offset + 4) return {Status::incomplete, {}, 0};
        offset += 4;
    }
    if (length > size - offset) return {Status::incomplete, {}, 0};
    Frame frame;
    frame.opcode = opcode;
    frame.fin = fin;
    frame.payload.reserve(static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint8_t byte = data[offset + i];
        frame.payload.push_back(static_cast<char>(
            masked ? byte ^ data[offset - 4 + i % 4] : byte));
    }
    if (opcode == 1 && !valid_utf8(frame.payload))
        return {Status::invalid, {}, 0};
    if (opcode == 8) {
        if (length == 1) return {Status::invalid, {}, 0};
        if (length >= 2) {
            const auto code = static_cast<std::uint16_t>(
                (std::uint16_t(static_cast<unsigned char>(frame.payload[0])) << 8) |
                static_cast<unsigned char>(frame.payload[1]));
            if (!valid_close_code(code) || !valid_utf8(frame.payload.substr(2)))
                return {Status::invalid, {}, 0};
        }
    }
    return {Status::ok, std::move(frame), offset + static_cast<std::size_t>(length)};
}
inline std::vector<std::uint8_t> encode_server(std::uint8_t opcode, const std::string& payload) {
    if (opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10)
        return {};
    if (payload.size() > max_frame_bytes || (opcode >= 8 && payload.size() > 125))
        return {};
    if (opcode == 1 && !valid_utf8(payload)) return {};
    if (opcode == 8) {
        if (payload.size() == 1) return {};
        if (payload.size() >= 2) {
            const auto code = static_cast<std::uint16_t>(
                (std::uint16_t(static_cast<unsigned char>(payload[0])) << 8) |
                static_cast<unsigned char>(payload[1]));
            if (!valid_close_code(code) || !valid_utf8(payload.substr(2))) return {};
        }
    }
    std::vector<std::uint8_t> wire;
    wire.push_back(static_cast<std::uint8_t>(0x80 | opcode));
    if (payload.size() < 126) wire.push_back(static_cast<std::uint8_t>(payload.size()));
    else if (payload.size() < 65536) {
        wire.push_back(126);
        wire.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
        wire.push_back(static_cast<std::uint8_t>(payload.size()));
    } else {
        wire.push_back(127);
        const auto len = static_cast<std::uint64_t>(payload.size());
        for (int i = 7; i >= 0; --i) wire.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
    }
    for (unsigned char byte : payload) wire.push_back(byte);
    return wire;
}
} // namespace noqeri_websocket
