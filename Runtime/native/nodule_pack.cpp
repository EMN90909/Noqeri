// SPDX-License-Identifier: Apache-2.0
// Deterministic Noqeri Nodule (NDL) packaging. This file deliberately uses
// only C++17 and the standard library so `noqeri pkg pack` works offline and
// does not require Node.js, npm, or an external archive utility.
#include "nodule_pack.hpp"
#include "../../Include/noqeri/nodule_archive.hpp"

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

namespace noqeri_nodule {

namespace {

constexpr std::size_t kNdlHeaderSize = 72;
constexpr std::size_t kNdlEntrySize = 56;
constexpr std::size_t kNdlMaxManifest = 16u * 1024u * 1024u;
constexpr std::size_t kNdlMaxPayload = 256u * 1024u * 1024u;
constexpr std::uint32_t kNdlMaxEntries = 65536u;

std::uint32_t adler32Update(std::uint32_t state, std::string_view data) {
    std::uint32_t a = state & 0xFFFFu;
    std::uint32_t b = (state >> 16) & 0xFFFFu;
    for (const unsigned char ch : data) {
        a = (a + ch) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::uint32_t adler32(std::string_view data) {
    return adler32Update(1u, data);
}

void writeU16Le(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void writeU32Le(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
    out.push_back(static_cast<char>((v >> 16) & 0xFFu));
    out.push_back(static_cast<char>((v >> 24) & 0xFFu));
}

std::uint16_t readU16Le(std::string_view s, std::size_t off) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(s[off]) |
        (static_cast<unsigned int>(static_cast<unsigned char>(s[off + 1])) << 8));
}

std::uint32_t readU32Le(std::string_view s, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string hexToRaw32(std::string_view hex) {
    std::string raw(32, '\0');
    if (hex.size() != 64) return raw;
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = hexNibble(hex[i * 2]);
        const int lo = hexNibble(hex[i * 2 + 1]);
        raw[i] = static_cast<char>(((hi < 0 ? 0 : hi) << 4) | (lo < 0 ? 0 : lo));
    }
    return raw;
}

std::string raw32ToHex(std::string_view raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (const unsigned char b : raw) {
        out.push_back(kHex[(b >> 4) & 0xFu]);
        out.push_back(kHex[b & 0xFu]);
    }
    return out;
}

bool isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isSafeArchivePath(std::string_view path, std::string& reason) {
    if (path.empty()) {
        reason = "empty entry path";
        return false;
    }
    if (path.size() > 512) {
        reason = "entry path exceeds maximum length";
        return false;
    }
    if (path.find('\0') != std::string_view::npos) {
        reason = "entry path contains NUL byte";
        return false;
    }
    if (path.find('\\') != std::string_view::npos) {
        reason = "entry path contains backslash";
        return false;
    }
    if (path.front() == '/') {
        reason = "entry path must not start with '/'";
        return false;
    }
    if (path.size() >= 2 && isAsciiAlpha(path[0]) && path[1] == ':') {
        reason = "entry path must not contain drive prefix";
        return false;
    }
    std::size_t pos = 0;
    while (pos <= path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t end = (slash == std::string_view::npos) ? path.size() : slash;
        const std::string_view seg = path.substr(pos, end - pos);
        if (seg.empty()) {
            reason = "entry path contains empty segment or trailing slash";
            return false;
        }
        if (seg == "." || seg == "..") {
            reason = "entry path contains traversal segment";
            return false;
        }
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return true;
}

std::string trimView(std::string_view sv) {
    std::size_t b = 0;
    std::size_t e = sv.size();
    while (b < e && (sv[b] == ' ' || sv[b] == '\t' || sv[b] == '\r')) ++b;
    while (e > b && (sv[e - 1] == ' ' || sv[e - 1] == '\t' || sv[e - 1] == '\r')) --e;
    return std::string(sv.substr(b, e - b));
}

std::string unquoteToml(std::string_view raw) {
    const std::string s = trimView(raw);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool parseNodulesToml(std::string_view toml, NdlManifest& out, std::string& error) {
    out = NdlManifest{};
    out.abiVersion = 0;
    out.languageVersion.clear();
    std::size_t pos = 0;
    while (pos < toml.size()) {
        std::size_t nl = toml.find('\n', pos);
        if (nl == std::string_view::npos) nl = toml.size();
        std::string line = trimView(toml.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "malformed nodules.toml line: " + line;
            return false;
        }
        const std::string key = trimView(std::string_view(line).substr(0, eq));
        const std::string val = trimView(std::string_view(line).substr(eq + 1));
        if (key == "name") out.name = unquoteToml(val);
        else if (key == "version") out.version = unquoteToml(val);
        else if (key == "language_version") out.languageVersion = unquoteToml(val);
        else if (key == "abi_version") {
            const std::string s = unquoteToml(val);
            if (s.empty()) {
                error = "empty abi_version in nodules.toml";
                return false;
            }
            for (const char ch : s) {
                if (ch < '0' || ch > '9') {
                    error = "invalid abi_version in nodules.toml: " + s;
                    return false;
                }
            }
            out.abiVersion = static_cast<std::uint32_t>(std::stoul(s));
        } else if (key == "entry") out.entry = unquoteToml(val);
        else if (key == "exports") {
            if (val.size() < 2 || val.front() != '[' || val.back() != ']') {
                error = "malformed exports list in nodules.toml";
                return false;
            }
            const std::string_view inner(val.data() + 1, val.size() - 2);
            std::size_t cur = 0;
            while (cur < inner.size()) {
                std::size_t comma = inner.find(',', cur);
                if (comma == std::string_view::npos) comma = inner.size();
                const std::string item = unquoteToml(inner.substr(cur, comma - cur));
                if (!item.empty()) out.exports.push_back(item);
                cur = comma + 1;
            }
        }
    }
    if (out.name.empty() || out.version.empty() || out.entry.empty()) {
        error = "nodules.toml missing required name, version, or entry";
        return false;
    }
    if (out.abiVersion != 2) {
        error = "incompatible .ndl ABI version: expected 2, got " + std::to_string(out.abiVersion);
        return false;
    }
    const bool langOk = (out.languageVersion == "0.1.5" || out.languageVersion == "0.1.5.1");
    if (!langOk) {
        error = "incompatible .ndl language_version: expected 0.1.5 or 0.1.5.1, got '" +
                out.languageVersion + "'";
        return false;
    }
    std::string pathErr;
    if (!isSafeArchivePath(out.entry, pathErr)) {
        error = "unsafe manifest entry path '" + out.entry + "': " + pathErr;
        return false;
    }
    for (const auto& exp : out.exports) {
        if (!isSafeArchivePath(exp, pathErr)) {
            error = "unsafe manifest export path '" + exp + "': " + pathErr;
            return false;
        }
    }
    return true;
}

std::string serializeNodulesToml(const NdlManifest& m) {
    std::ostringstream out;
    out << "name = \"" << m.name << "\"\n"
        << "version = \"" << m.version << "\"\n"
        << "language_version = \"" << (m.languageVersion.empty() ? "0.1.5.1" : m.languageVersion) << "\"\n"
        << "abi_version = " << (m.abiVersion == 0 ? 2u : m.abiVersion) << "\n"
        << "entry = \"" << m.entry << "\"\n"
        << "exports = [";
    for (std::size_t i = 0; i < m.exports.size(); ++i) {
        if (i > 0) out << ", ";
        out << "\"" << m.exports[i] << "\"";
    }
    out << "]\n";
    return out.str();
}

} // namespace

const NdlEntry* NdlArchive::findEntry(std::string_view path) const {
    for (const auto& e : entries) {
        if (e.path == path) return &e;
    }
    return nullptr;
}

std::string buildNdlArchive(const NdlManifest& manifest,
                            const std::vector<std::pair<std::string, std::string>>& files) {
    std::string pathErr;
    if (!isSafeArchivePath(manifest.entry, pathErr)) {
        throw std::runtime_error("unsafe manifest entry path: " + pathErr);
    }
    for (const auto& exp : manifest.exports) {
        if (!isSafeArchivePath(exp, pathErr)) {
            throw std::runtime_error("unsafe manifest export path: " + pathErr);
        }
    }
    if (files.empty() || files.size() > kNdlMaxEntries) {
        throw std::runtime_error("invalid .ndl entry count");
    }
    const std::string toml = serializeNodulesToml(manifest);
    std::string payload;
    std::string entryTable;
    entryTable.reserve(files.size() * kNdlEntrySize);

    for (const auto& [relPath, content] : files) {
        if (!isSafeArchivePath(relPath, pathErr)) {
            throw std::runtime_error("unsafe .ndl entry path '" + relPath + "': " + pathErr);
        }
        const std::uint32_t pathOff = static_cast<std::uint32_t>(payload.size());
        const std::uint32_t pathLen = static_cast<std::uint32_t>(relPath.size());
        payload.append(relPath);
        const std::uint32_t dataOff = static_cast<std::uint32_t>(payload.size());
        const std::uint32_t dataLen = static_cast<std::uint32_t>(content.size());
        payload.append(content);

        const std::uint32_t entryAdler = adler32(content);
        const std::string entryShaRaw = hexToRaw32(sha256(content));

        writeU32Le(entryTable, pathOff);
        writeU32Le(entryTable, pathLen);
        writeU32Le(entryTable, dataOff);
        writeU32Le(entryTable, dataLen);
        writeU32Le(entryTable, entryAdler);
        writeU32Le(entryTable, 0u);
        entryTable.append(entryShaRaw);
    }

    const std::uint32_t manifestOffset = static_cast<std::uint32_t>(kNdlHeaderSize);
    const std::uint32_t manifestLength = static_cast<std::uint32_t>(toml.size());
    const std::uint32_t entryTableOffset = manifestOffset + manifestLength;
    const std::uint32_t entryCount = static_cast<std::uint32_t>(files.size());
    const std::uint32_t payloadOffset =
        entryTableOffset + static_cast<std::uint32_t>(entryTable.size());
    const std::uint32_t payloadLength = static_cast<std::uint32_t>(payload.size());

    std::string hdrPrefix;
    hdrPrefix.reserve(40);
    hdrPrefix.append("NDL\x01", 4);
    writeU16Le(hdrPrefix, 1u);
    writeU16Le(hdrPrefix, 0u);
    writeU32Le(hdrPrefix, manifestOffset);
    writeU32Le(hdrPrefix, manifestLength);
    writeU32Le(hdrPrefix, entryTableOffset);
    writeU32Le(hdrPrefix, entryCount);
    writeU32Le(hdrPrefix, payloadOffset);
    writeU32Le(hdrPrefix, payloadLength);

    std::uint32_t frameAdler = adler32Update(1u, hdrPrefix);
    frameAdler = adler32Update(frameAdler, toml);
    frameAdler = adler32Update(frameAdler, entryTable);
    frameAdler = adler32Update(frameAdler, payload);

    writeU32Le(hdrPrefix, frameAdler);
    writeU32Le(hdrPrefix, 0u);

    Sha256 fullHash;
    fullHash.update(hdrPrefix);
    fullHash.update(toml);
    fullHash.update(entryTable);
    fullHash.update(payload);
    const std::string contentShaRaw = hexToRaw32(fullHash.finish());

    std::string out;
    out.reserve(kNdlHeaderSize + toml.size() + entryTable.size() + payload.size());
    out.append(hdrPrefix);
    out.append(contentShaRaw);
    out.append(toml);
    out.append(entryTable);
    out.append(payload);
    return out;
}

bool loadNdlArchiveFromBytes(std::string_view bytes, NdlArchive& out, std::string& error) {
    out = NdlArchive{};
    if (bytes.size() < kNdlHeaderSize) {
        error = "malformed .ndl archive: header shorter than 72 bytes";
        return false;
    }
    if (bytes.substr(0, 4) != std::string_view("NDL\x01", 4)) {
        error = "invalid .ndl magic header (expected NDL\\x01)";
        return false;
    }
    const std::uint16_t formatVersion = readU16Le(bytes, 4);
    const std::uint16_t flags = readU16Le(bytes, 6);
    if (formatVersion != 1u) {
        error = "unsupported .ndl format version: " + std::to_string(formatVersion);
        return false;
    }
    if (flags != 0u) {
        error = "unsupported .ndl flags: " + std::to_string(flags);
        return false;
    }
    const std::uint32_t manifestOffset = readU32Le(bytes, 8);
    const std::uint32_t manifestLength = readU32Le(bytes, 12);
    const std::uint32_t entryTableOffset = readU32Le(bytes, 16);
    const std::uint32_t entryCount = readU32Le(bytes, 20);
    const std::uint32_t payloadOffset = readU32Le(bytes, 24);
    const std::uint32_t payloadLength = readU32Le(bytes, 28);
    const std::uint32_t expectedAdler = readU32Le(bytes, 32);
    const std::uint32_t reserved = readU32Le(bytes, 36);
    if (reserved != 0u) {
        error = "malformed .ndl header: non-zero reserved field";
        return false;
    }
    const std::string expectedShaHex = raw32ToHex(bytes.substr(40, 32));

    const std::uint64_t totalSize = bytes.size();
    if (manifestOffset < kNdlHeaderSize || manifestLength == 0 ||
        manifestLength > kNdlMaxManifest ||
        static_cast<std::uint64_t>(manifestOffset) + manifestLength > totalSize) {
        error = "malformed .ndl archive: invalid manifest offset/length";
        return false;
    }
    const std::uint64_t manifestEnd = static_cast<std::uint64_t>(manifestOffset) + manifestLength;
    if (entryCount == 0 || entryCount > kNdlMaxEntries ||
        entryTableOffset < manifestEnd ||
        static_cast<std::uint64_t>(entryTableOffset) + static_cast<std::uint64_t>(entryCount) * kNdlEntrySize > totalSize) {
        error = "malformed .ndl archive: invalid entry table offset/count";
        return false;
    }
    const std::uint64_t entryTableEnd =
        static_cast<std::uint64_t>(entryTableOffset) + static_cast<std::uint64_t>(entryCount) * kNdlEntrySize;
    if (payloadOffset < entryTableEnd || payloadLength > kNdlMaxPayload ||
        static_cast<std::uint64_t>(payloadOffset) + payloadLength > totalSize) {
        error = "malformed .ndl archive: invalid payload offset/length";
        return false;
    }
    const std::string_view manifestSlice = bytes.substr(manifestOffset, manifestLength);
    const std::string_view tableSlice =
        bytes.substr(entryTableOffset, static_cast<std::size_t>(entryCount) * kNdlEntrySize);
    const std::string_view payloadSlice = bytes.substr(payloadOffset, payloadLength);

    std::uint32_t actualAdler = adler32Update(1u, bytes.substr(0, 32));
    actualAdler = adler32Update(actualAdler, manifestSlice);
    actualAdler = adler32Update(actualAdler, tableSlice);
    actualAdler = adler32Update(actualAdler, payloadSlice);
    if (actualAdler != expectedAdler) {
        error = "corrupt .ndl archive: adler32 frame checksum mismatch";
        return false;
    }

    Sha256 fullHash;
    fullHash.update(bytes.substr(0, 40));
    fullHash.update(manifestSlice);
    fullHash.update(tableSlice);
    fullHash.update(payloadSlice);
    const std::string actualShaHex = fullHash.finish();
    if (actualShaHex != expectedShaHex) {
        error = "corrupt .ndl archive: SHA-256 content checksum mismatch";
        return false;
    }

    if (!parseNodulesToml(manifestSlice, out.manifest, error)) {
        return false;
    }

    out.entries.reserve(entryCount);
    for (std::uint32_t idx = 0; idx < entryCount; ++idx) {
        const std::size_t recOff = static_cast<std::size_t>(idx) * kNdlEntrySize;
        const std::uint32_t pathOff = readU32Le(tableSlice, recOff + 0);
        const std::uint32_t pathLen = readU32Le(tableSlice, recOff + 4);
        const std::uint32_t dataOff = readU32Le(tableSlice, recOff + 8);
        const std::uint32_t dataLen = readU32Le(tableSlice, recOff + 12);
        const std::uint32_t recAdler = readU32Le(tableSlice, recOff + 16);
        const std::uint32_t recReserved = readU32Le(tableSlice, recOff + 20);
        const std::string recShaHex = raw32ToHex(tableSlice.substr(recOff + 24, 32));
        if (recReserved != 0u) {
            error = "malformed .ndl entry record: non-zero reserved field";
            return false;
        }
        if (pathLen == 0 || pathLen > 512 ||
            static_cast<std::uint64_t>(pathOff) + pathLen > payloadLength ||
            static_cast<std::uint64_t>(dataOff) + dataLen > payloadLength) {
            error = "malformed .ndl entry bounds";
            return false;
        }
        const std::string_view entryPath = payloadSlice.substr(pathOff, pathLen);
        std::string pathErr;
        if (!isSafeArchivePath(entryPath, pathErr)) {
            error = "unsafe .ndl entry path '" + std::string(entryPath) + "': " + pathErr;
            return false;
        }
        if (out.findEntry(entryPath) != nullptr) {
            error = "duplicate .ndl entry path: " + std::string(entryPath);
            return false;
        }
        const std::string_view entryData = payloadSlice.substr(dataOff, dataLen);
        if (adler32(entryData) != recAdler) {
            error = "corrupt .ndl entry '" + std::string(entryPath) + "': adler32 mismatch";
            return false;
        }
        const std::string actualEntrySha = sha256(entryData);
        if (actualEntrySha != recShaHex) {
            error = "corrupt .ndl entry '" + std::string(entryPath) + "': SHA-256 mismatch";
            return false;
        }
        NdlEntry entry;
        entry.path = std::string(entryPath);
        entry.data = std::string(entryData);
        entry.adler32 = recAdler;
        entry.sha256Hex = actualEntrySha;
        out.entries.push_back(std::move(entry));
    }

    if (out.findEntry(out.manifest.entry) == nullptr) {
        error = ".ndl archive missing declared entry module: " + out.manifest.entry;
        return false;
    }
    for (const auto& exp : out.manifest.exports) {
        if (out.findEntry(exp) == nullptr) {
            error = ".ndl archive missing declared export module: " + exp;
            return false;
        }
    }
    return true;
}

bool loadNdlArchiveFromFile(const std::filesystem::path& path, NdlArchive& out, std::string& error) {
    out = NdlArchive{};
    std::error_code ec;
    const auto st = fs::symlink_status(path, ec);
    if (ec || !fs::exists(st)) {
        error = "cannot inspect .ndl archive: " + path.string();
        return false;
    }
    if (fs::is_symlink(st)) {
        error = "refusing symlink .ndl archive path: " + path.string();
        return false;
    }
    if (!fs::is_regular_file(st)) {
        error = ".ndl archive path is not a regular file: " + path.string();
        return false;
    }
    const auto fsize = fs::file_size(path, ec);
    if (ec || fsize > kNdlMaxManifest + kNdlMaxPayload + kNdlHeaderSize + 1024u * 1024u) {
        error = ".ndl archive size invalid or exceeds limit: " + path.string();
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open .ndl archive: " + path.string();
        return false;
    }
    std::string bytes(static_cast<std::size_t>(fsize), '\0');
    if (fsize > 0 && !in.read(bytes.data(), static_cast<std::streamsize>(fsize))) {
        error = "failed reading .ndl archive: " + path.string();
        return false;
    }
    return loadNdlArchiveFromBytes(bytes, out, error);
}

} // namespace noqeri_nodule
