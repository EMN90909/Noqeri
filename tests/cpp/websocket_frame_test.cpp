#include "noqeri/websocket_frame.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using noqeri_websocket::Status;
using noqeri_websocket::decode;
using noqeri_websocket::encode_server;

static std::vector<std::uint8_t> masked(std::uint8_t opcode, const std::string& text) {
    std::vector<std::uint8_t> wire{static_cast<std::uint8_t>(0x80 | opcode)};
    if (text.size() < 126) wire.push_back(static_cast<std::uint8_t>(0x80 | text.size()));
    else if (text.size() < 65536) {
        wire.push_back(0xfe);
        wire.push_back(static_cast<std::uint8_t>(text.size() >> 8));
        wire.push_back(static_cast<std::uint8_t>(text.size()));
    } else {
        wire.push_back(0xff);
        const auto len = static_cast<std::uint64_t>(text.size());
        for (int i = 7; i >= 0; --i) wire.push_back(static_cast<std::uint8_t>(len >> (i * 8)));
    }
    const std::array<std::uint8_t, 4> mask{0x12, 0x34, 0x56, 0x78};
    wire.insert(wire.end(), mask.begin(), mask.end());
    for (std::size_t i = 0; i < text.size(); ++i)
        wire.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(text[i]) ^ mask[i % 4]));
    return wire;
}
static void reject(std::vector<std::uint8_t> bytes, bool from_client = true) {
    assert(decode(bytes.data(), bytes.size(), from_client).status == Status::invalid);
}
int main() {
    auto hello=masked(1,"Hello, Noqeri");
    auto parsed=decode(hello.data(),hello.size(),true);
    assert(parsed.status==Status::ok && parsed.consumed==hello.size());
    assert(parsed.frame.payload=="Hello, Noqeri" && parsed.frame.opcode==1);
    assert(decode(hello.data(),hello.size()-1,true).status==Status::incomplete);
    reject(hello,false);
    auto extra=hello;extra.push_back(0x81);
    assert(decode(extra.data(),extra.size(),true).consumed==hello.size());

    auto server=encode_server(1,"Hello");
    assert(!server.empty());
    assert(decode(server.data(),server.size(),false).frame.payload=="Hello");
    reject(server,true);
    auto binary=masked(2,std::string("\x00\xfe",2));
    assert(decode(binary.data(),binary.size(),true).frame.payload==std::string("\x00\xfe",2));

    auto pong=masked(10,"ok");
    assert(decode(pong.data(),pong.size(),true).status==Status::ok);
    auto ping=masked(9,"ok");
    assert(decode(ping.data(),ping.size(),true).status==Status::ok);
    auto close=masked(8,std::string("\x03\xe8",2)+"Normal");
    assert(decode(close.data(),close.size(),true).status==Status::ok);
    reject(masked(8,std::string("\x03\xed",2))); // 1005 reserved
    reject(masked(8,std::string("\x07\xd0",2))); // 2000 reserved
    reject(masked(8,"x")); // close payload can't have only one byte
    reject(masked(1,std::string("\xc0\x80",2))); // overlong UTF-8
    reject(masked(1,std::string("\xed\xa0\x80",3))); // surrogate UTF-8
    reject(masked(1,std::string("\xf4\x90\x80\x80",4))); // > 0x10ffff
    assert(decode(masked(1,std::string("\xe2\x82\xac",3)).data(),9,true).status==Status::ok);
    assert(encode_server(1,std::string("\xc0\x80",2)).empty());
    assert(encode_server(9,std::string(126,'a')).empty());
    assert(encode_server(8,"x").empty());

    auto rsv=hello;rsv[0]|=0x40;reject(rsv);
    auto fragmented=hello;fragmented[0]&=0x7f;reject(fragmented);
    auto continuation=hello;continuation[0]=0x80;reject(continuation);
    auto unsupported=hello;unsupported[0]=0x83;reject(unsupported);
    auto invalidCode=masked(8,std::string("\x0b\xb8",2));
    assert(decode(invalidCode.data(),invalidCode.size(),true).status==Status::ok); // 3000 allowed

    auto longMessage=masked(1,std::string(126,'A'));
    assert(decode(longMessage.data(),longMessage.size(),true).status==Status::ok);
    auto nonminimal=longMessage;nonminimal[2]=0;nonminimal[3]=125;reject(nonminimal);
    auto huge=masked(2,std::string(noqeri_websocket::max_frame_bytes+1,'x'));
    reject(huge);
    auto largePing=masked(9,std::string(126,'x'));reject(largePing);
    assert(encode_server(2,std::string(65536,'z')).size()==65546);
    std::cout<<"Noqeri RFC6455 frame qualification passed\n";
}
