// SPDX-License-Identifier: Apache-2.0
// Deterministic Noqeri Nodule (NDL) packaging. This file deliberately uses
// only C++17 and the standard library so `noqeri pkg pack` works offline and
// does not require Node.js, npm, or an external archive utility.
#include "nodule_pack.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(NOQERI_REGISTRY_NETWORK)
#include <curl/curl.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxFiles = 256;
constexpr std::size_t kMaxSourceBytes = 9 * 1024 * 1024;
constexpr std::size_t kMaxArchiveBytes = 8 * 1024 * 1024;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

void writeFile(const fs::path& path, std::string_view bytes) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) fail("cannot write " + path.string());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) fail("write failed " + path.string());
}

bool safePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find('\0') != std::string::npos || fs::path(path).is_absolute() ||
        fs::path(path).has_root_name()) return false;
    for (const auto& component : fs::path(path)) {
        if (component.empty() || component == "." || component == "..") return false;
    }
    return true;
}

std::string jsonEscape(const std::string& value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '\"') out.push_back('\\');
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        out.push_back(c);
    }
    out.push_back('\"');
    return out;
}

struct FileEntry { std::string name; std::string bytes; };

noqeri::nodule::PackageInfo readManifest(const fs::path& root) {
    const auto path = root / "package.noqeri";
    const auto source = readFile(path);
    std::smatch match;
    const std::regex name(R"re(\bname\s*:\s*"([a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*)")re");
    const std::regex version(R"re(\bversion\s*:\s*"((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)")re");
    const std::regex entry(R"re(\bentry\s*:\s*"([^"\\]+\.noqeri)")re");
    if (!std::regex_search(source, match, name)) fail("package.noqeri has no valid name");
    noqeri::nodule::PackageInfo info;
    info.coordinate = match[1].str();
    if (!std::regex_search(source, match, version)) fail("package.noqeri has no valid version");
    info.version = match[1].str();
    if (!std::regex_search(source, match, entry)) fail("package.noqeri has no valid entry");
    info.entry = match[1].str();
    if (!safePath(info.entry)) fail("package entry is unsafe");
    info.manifest = source;
    return info;
}

void collectFiles(const fs::path& root, const fs::path& directory,
                  std::vector<FileEntry>& files, std::size_t& bytes) {
    if (files.size() > kMaxFiles) fail("package contains too many files");
    std::error_code error;
    fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error);
    if (error) fail("cannot inspect package directory: " + error.message());
    for (const auto& item : iterator) {
        const auto name = item.path().filename().string();
        if (item.is_directory(error)) {
            if (name == ".git" || name == ".noqeri" || name == "build" || name == "dist" || name == "target") continue;
            collectFiles(root, item.path(), files, bytes);
            continue;
        }
        if (error) fail("cannot inspect package entry: " + error.message());
        if (!item.is_regular_file(error)) {
            if (item.is_symlink(error)) fail("symlinks are not allowed in a nodule: " + item.path().string());
            continue;
        }
        const auto relative = fs::relative(item.path(), root, error).generic_string();
        if (error || !safePath(relative)) fail("unsafe package path: " + relative);
        if (relative == ".noqeri-integrity.json") continue;
        auto data = readFile(item.path());
        bytes += data.size();
        if (bytes > kMaxSourceBytes) fail("package source exceeds 9 MiB");
        files.push_back({relative, std::move(data)});
        if (files.size() > kMaxFiles) fail("package contains too many files");
    }
}

void putOctal(std::array<unsigned char, 512>& header, std::size_t offset,
              std::size_t width, std::uint64_t value) {
    std::ostringstream text;
    text << std::oct << std::setw(static_cast<int>(width - 1)) << std::setfill('0') << value;
    const auto encoded = text.str();
    std::copy(encoded.begin(), encoded.end(), header.begin() + static_cast<std::ptrdiff_t>(offset));
    header[offset + width - 1] = 0;
}

std::array<unsigned char, 512> tarHeader(const FileEntry& file) {
    if (file.name.size() > 100) fail("package path exceeds tar header limit: " + file.name);
    std::array<unsigned char, 512> header{};
    std::copy(file.name.begin(), file.name.end(), header.begin());
    putOctal(header, 100, 8, 0644);
    putOctal(header, 108, 8, 0);
    putOctal(header, 116, 8, 0);
    putOctal(header, 124, 12, file.bytes.size());
    putOctal(header, 136, 12, 0);
    std::fill(header.begin() + 148, header.begin() + 156, ' ');
    header[156] = '0';
    const std::array<unsigned char, 6> magic{'u','s','t','a','r',0};
    std::copy(magic.begin(), magic.end(), header.begin() + 257);
    header[263] = '0'; header[264] = '0';
    const std::uint32_t checksum = std::accumulate(header.begin(), header.end(), 0u);
    putOctal(header, 148, 8, checksum);
    return header;
}

std::string tar(const std::vector<FileEntry>& files) {
    std::string output;
    for (const auto& file : files) {
        const auto header = tarHeader(file);
        output.append(reinterpret_cast<const char*>(header.data()), header.size());
        output += file.bytes;
        output.append((512 - (file.bytes.size() % 512)) % 512, '\0');
    }
    output.append(1024, '\0');
    return output;
}

std::uint32_t crc32(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
    }
    return ~crc;
}

std::uint32_t adler32(std::string_view bytes) {
    std::uint32_t a = 1, b = 0;
    for (const unsigned char byte : bytes) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

void appendLe32(std::string& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) output.push_back(static_cast<char>((value >> shift) & 0xff));
}

// A valid deterministic gzip stream using DEFLATE stored blocks. It trades a
// little compression ratio for a tiny, dependency-free and reproducible packer.
std::string gzipStored(std::string_view input) {
    std::string output("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    if (input.empty()) output += "\x01\x00\x00\xff\xff";
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto count = std::min<std::size_t>(65535, input.size() - offset);
        const bool final = offset + count == input.size();
        output.push_back(static_cast<char>(final ? 1 : 0));
        const auto length = static_cast<std::uint16_t>(count);
        const auto inverse = static_cast<std::uint16_t>(~length);
        output.push_back(static_cast<char>(length & 0xff)); output.push_back(static_cast<char>(length >> 8));
        output.push_back(static_cast<char>(inverse & 0xff)); output.push_back(static_cast<char>(inverse >> 8));
        output.append(input.substr(offset, count)); offset += count;
    }
    appendLe32(output, crc32(input));
    appendLe32(output, static_cast<std::uint32_t>(input.size()));
    return output;
}

class Sha256 {
public:
    void update(std::string_view text) { for (const unsigned char byte : text) updateByte(byte); }
    std::string finish() {
        const auto original = bitLength_;
        updateByte(0x80);
        while (bufferSize_ != 56) updateByte(0);
        for (int shift = 56; shift >= 0; shift -= 8) updateByte(static_cast<unsigned char>(original >> shift));
        std::ostringstream result; result << std::hex << std::setfill('0');
        for (const auto word : state_) result << std::setw(8) << word;
        return result.str();
    }
private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void updateByte(unsigned char byte) {
        buffer_[bufferSize_++] = byte; bitLength_ += 8;
        if (bufferSize_ == 64) { transform(); bufferSize_ = 0; }
    }
    void transform() {
        static constexpr std::array<std::uint32_t, 64> k = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) w[i] = (static_cast<std::uint32_t>(buffer_[i*4]) << 24) |
            (static_cast<std::uint32_t>(buffer_[i*4+1]) << 16) | (static_cast<std::uint32_t>(buffer_[i*4+2]) << 8) | buffer_[i*4+3];
        for (std::size_t i = 16; i < 64; ++i) { auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3); auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (std::size_t i=0;i<64;++i) { auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25); auto ch=(e&f)^((~e)&g); auto t1=h+s1+ch+k[i]+w[i]; auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22); auto maj=(a&b)^(a&c)^(b&c); auto t2=s0+maj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    std::array<std::uint32_t,8> state_{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::array<unsigned char,64> buffer_{}; std::size_t bufferSize_=0; std::uint64_t bitLength_=0;
};

std::string sha256(std::string_view value) { Sha256 hash; hash.update(value); return hash.finish(); }

#if defined(NOQERI_REGISTRY_NETWORK)
size_t receive(char* data, size_t size, size_t count, void* target) {
    auto* output = static_cast<std::string*>(target); const auto length = size * count;
    output->append(data, length); return length;
}

std::string base64(std::string_view input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output; output.reserve((input.size()+2)/3*4);
    for (std::size_t i=0;i<input.size();i+=3) { const auto a=static_cast<unsigned char>(input[i]); const auto b=i+1<input.size()?static_cast<unsigned char>(input[i+1]):0; const auto c=i+2<input.size()?static_cast<unsigned char>(input[i+2]):0; output += alphabet[a>>2]; output += alphabet[((a&3)<<4)|(b>>4)]; output += i+1<input.size()?alphabet[((b&15)<<2)|(c>>6)]:'='; output += i+2<input.size()?alphabet[c&63]:'='; }
    return output;
}

int publishJson(const std::string& url, const std::string& token, const noqeri::nodule::PackageInfo& info,
                const std::string& archive) {
    CURL* curl = curl_easy_init(); if (!curl) fail("libcurl initialization failed");
    const auto body = std::string("{\"namespace\":") + jsonEscape(info.coordinate.substr(0, info.coordinate.find('/'))) +
        ",\"name\":" + jsonEscape(info.coordinate.substr(info.coordinate.find('/') + 1)) +
        ",\"version\":" + jsonEscape(info.version) + ",\"entry\":" + jsonEscape(info.entry) +
        ",\"archive_sha256\":" + jsonEscape(sha256(archive)) + ",\"archive_base64\":" + jsonEscape(base64(archive)) + "}";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
    std::string response; curl_easy_setopt(curl, CURLOPT_URL, (url + "/v1/publish").c_str()); curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str()); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    const auto result = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status); curl_slist_free_all(headers); curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300) fail("private registry publish failed (HTTP " + std::to_string(status) + "): " + response);
    return 0;
}
#endif

} // namespace

namespace noqeri::nodule {

PackageInfo pack(const fs::path& requestedRoot, const fs::path& requestedOutput) {
    const auto root = fs::absolute(requestedRoot);
    if (!fs::is_directory(root)) fail("package root is not a directory: " + root.string());
    auto info = readManifest(root);
    std::vector<FileEntry> files; std::size_t sourceBytes = 0; collectFiles(root, root, files, sourceBytes);
    if (std::none_of(files.begin(), files.end(), [](const FileEntry& f){ return f.name == "package.noqeri"; })) fail("package.noqeri is missing");
    if (std::none_of(files.begin(), files.end(), [&](const FileEntry& f){ return f.name == info.entry; })) fail("package entry is missing: " + info.entry);
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b){ return a.name < b.name; });
    const auto archive = gzipStored(tar(files));
    if (archive.size() > kMaxArchiveBytes) fail("nodule archive exceeds 8 MiB");
    auto output = requestedOutput;
    if (output.empty()) output = fs::path("target") / (info.coordinate.substr(info.coordinate.find('/')+1) + "-" + info.version + ".ndl");
    writeFile(output, archive);
    std::cout << "packed " << info.coordinate << "@" << info.version << " -> " << output.string() << "\n"
              << "archive-sha256: sha256:" << sha256(archive) << "\n";
    return info;
}

int publish(const fs::path& root, const fs::path& output, const std::string& registry_url, const std::string& token) {
    const auto info = pack(root, output); const auto archive = readFile(output.empty() ? fs::path("target") / (info.coordinate.substr(info.coordinate.find('/')+1) + "-" + info.version + ".ndl") : output);
#if defined(NOQERI_REGISTRY_NETWORK)
    if (registry_url.empty() || token.empty()) fail("publish requires NOQERI_PRIVATE_REGISTRY_URL and NOQERI_REGISTRY_TOKEN");
    publishJson(registry_url, token, info, archive);
    std::cout << "published " << info.coordinate << "@" << info.version << "\n"; return 0;
#else
    (void)registry_url; (void)token; (void)archive; fail("native HTTPS unavailable: rebuild Noqeri SDK with libcurl");
#endif
}

} // namespace noqeri::nodule
