#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/resource.h>

namespace {

void ensureCompilerStackBudget() {
    constexpr rlim_t kDesired = static_cast<rlim_t>(512ull * 1024ull * 1024ull);
    rlimit limit{};
    if (getrlimit(RLIMIT_STACK, &limit) != 0)
        throw std::runtime_error("cannot read RLIMIT_STACK for native compiler");
    if (limit.rlim_cur >= kDesired || limit.rlim_cur == RLIM_INFINITY) return;
    rlim_t target = kDesired;
    if (limit.rlim_max != RLIM_INFINITY && target > limit.rlim_max) target = limit.rlim_max;
    if (target <= limit.rlim_cur)
        throw std::runtime_error("native compiler requires a larger stack budget; hard RLIMIT_STACK is too small");
    limit.rlim_cur = target;
    if (setrlimit(RLIMIT_STACK, &limit) != 0)
        throw std::runtime_error("cannot raise RLIMIT_STACK for native compiler arenas");
}

struct Slice {
    std::uint8_t* data;
    std::uint64_t size;
};

extern "C" std::int64_t stage2CompileToNativeObject(
    const Slice* source,
    std::int64_t target,
    std::int64_t debug,
    std::int64_t unsafeAllowed,
    const Slice* output);

std::string trim(std::string value) {
    auto first = std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); });
    auto last = std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

bool redundantForwardPrototype(const std::string& clean) {
    if (clean.rfind("function ", 0) != 0) return false;
    if (clean.find('{') != std::string::npos) return false;
    return clean.find('(') != std::string::npos && clean.find(')') != std::string::npos;
}

std::vector<std::uint8_t> bundleManifest(const std::string& rootPath, const std::string& manifestPath) {
    namespace fs = std::filesystem;
    const fs::path root = fs::weakly_canonical(fs::path(rootPath));
    std::ifstream manifest(manifestPath);
    if (!manifest) throw std::runtime_error("cannot open source manifest " + manifestPath);

    std::ostringstream merged;
    merged << "module noqeri.compiler.selfhost.bundle\n";
    std::unordered_set<std::string> seen;
    std::string relative;
    std::size_t sourceCount = 0;

    while (std::getline(manifest, relative)) {
        relative = trim(relative);
        if (relative.empty() || relative[0] == '#') continue;

        const fs::path listed(relative);
        if (listed.is_absolute() || listed.has_root_name() || listed.has_root_directory())
            throw std::runtime_error("source manifest requires repository-relative paths: " + relative);
        if (listed.extension() != ".noqeri")
            throw std::runtime_error("source manifest accepts only .noqeri files: " + relative);

        const fs::path candidate = fs::weakly_canonical(root / listed);
        const auto rel = candidate.lexically_relative(root);
        if (rel.empty() || *rel.begin() == ".." || rel == ".")
            throw std::runtime_error("source manifest escapes repository root: " + relative);

        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec) || ec)
            throw std::runtime_error("source manifest entry is not a regular file: " + relative);
        if (!seen.insert(candidate.generic_string()).second)
            throw std::runtime_error("duplicate source in manifest: " + relative);

        ++sourceCount;
        std::ifstream input(candidate, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open compiler source " + candidate.string());
        std::string line;
        while (std::getline(input, line)) {
            const auto clean = trim(line);
            if (clean.rfind("module ", 0) == 0 || clean.rfind("import ", 0) == 0) continue;
            if (redundantForwardPrototype(clean)) continue;
            merged << line << '\n';
        }
        merged << '\n';
    }

    if (manifest.bad()) throw std::runtime_error("failed reading source manifest " + manifestPath);
    if (sourceCount == 0) throw std::runtime_error("source manifest contains no .noqeri files: " + manifestPath);
    const auto text = merged.str();
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes, std::size_t count) {
    if (count > bytes.size()) throw std::runtime_error("write size exceeds output buffer");
    std::filesystem::create_directories(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
    if (!out) throw std::runtime_error("failed writing " + path.string());
}

int compileManifest(int argc, char** argv) {
    if (argc < 5 || argc > 6) {
        std::cerr << "usage: noqeri-native-stage compile-manifest <repo-root> <manifest> <output.o> [target]\n";
        return 2;
    }
    const std::int64_t target = argc == 6 ? std::stoll(argv[5]) : 3;
    if (target != 3) {
        std::cerr << "native self-host compiler currently qualifies Linux x86-64 target 3 only\n";
        return 2;
    }

    auto source = bundleManifest(argv[2], argv[3]);
    const std::size_t minimum = 64u * 1024u * 1024u;
    const std::size_t scaled = source.size() > (1024u * 1024u * 1024u - 16u * 1024u * 1024u) / 48u
        ? 1024u * 1024u * 1024u
        : source.size() * 48u + 16u * 1024u * 1024u;
    const std::size_t capacity = std::max(minimum, std::min<std::size_t>(scaled, 1024u * 1024u * 1024u));
    std::vector<std::uint8_t> output(capacity);

    Slice sourceSlice{source.data(), static_cast<std::uint64_t>(source.size())};
    Slice outputSlice{output.data(), static_cast<std::uint64_t>(output.size())};
    const auto written = stage2CompileToNativeObject(&sourceSlice, target, 1, 1, &outputSlice);
    if (written < 0) {
        std::cerr << "native Stage-2 compiler failed to emit object: " << written << "\n";
        return 1;
    }
    if (static_cast<std::uint64_t>(written) > output.size()) {
        std::cerr << "native Stage-2 compiler returned an invalid object size\n";
        return 1;
    }

    writeFile(argv[4], output, static_cast<std::size_t>(written));
    std::cout << "wrote native compiler object " << argv[4] << " (" << written << " bytes)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        ensureCompilerStackBudget();
        if (argc >= 2 && std::string(argv[1]) == "compile-manifest") return compileManifest(argc, argv);
        std::cerr << "usage: noqeri-native-stage compile-manifest <repo-root> <manifest> <output.o> [target]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "noqeri-native-stage: " << error.what() << "\n";
        return 1;
    }
}
