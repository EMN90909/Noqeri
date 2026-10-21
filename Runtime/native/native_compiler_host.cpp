#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <pthread.h>

struct NqSlice {
    std::uint8_t* data;
    std::uint64_t size;
};

extern "C" std::int64_t nativeCompileToAssembly(
    NqSlice* source,
    std::int64_t target,
    std::int64_t debug,
    std::int64_t unsafeAllowed,
    NqSlice* output);

namespace {
struct CompileJob {
    NqSlice source{};
    NqSlice output{};
    std::int64_t target{3};
    std::int64_t unsafeAllowed{0};
    std::int64_t result{-1};
};

void* compileOnLargeStack(void* opaque) {
    auto* job = static_cast<CompileJob*>(opaque);
    job->result = nativeCompileToAssembly(&job->source, job->target, 0, job->unsafeAllowed, &job->output);
    return nullptr;
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("cannot size input: " + path);
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("cannot read input: " + path);
    return bytes;
}

void writeFile(const std::string& path, const std::uint8_t* data, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output: " + path);
    if (size) output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) throw std::runtime_error("cannot write output: " + path);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--engine") {
            std::cout << "selfhost-native-linux-x86_64\n";
            return 0;
        }
        if (argc < 3 || argc > 4) {
            std::cerr << "usage: noqeri-native <source.noqeri> <output.s> [target]\n";
            return 2;
        }

        const std::int64_t target = argc >= 4 ? std::stoll(argv[3]) : 3;
        const std::int64_t unsafeAllowed = argc >= 5 ? std::stoll(argv[4]) : 0;
        if (unsafeAllowed != 0 && unsafeAllowed != 1) {
            std::cerr << "noqeri-native: unsafe must be 0 or 1\\n";
            return 2;
        }
        if (target != 3) {
            std::cerr << "noqeri-native: qualified native target is currently Linux x86-64 (target 3)\n";
            return 2;
        }

        auto source = readFile(argv[1]);
        constexpr std::size_t kFloor = 4u * 1024u * 1024u;
        if (source.size() > (std::numeric_limits<std::size_t>::max() - kFloor) / 256u)
            throw std::runtime_error("assembly output capacity overflow");
        const std::size_t capacity = std::max<std::size_t>(kFloor, source.size() * 256u + kFloor);
        std::vector<std::uint8_t> output(capacity);

        CompileJob job;
        job.source = {source.data(), static_cast<std::uint64_t>(source.size())};
        job.output = {output.data(), static_cast<std::uint64_t>(output.size())};
        job.target = target;
        job.unsafeAllowed = unsafeAllowed;

        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0) throw std::runtime_error("pthread_attr_init failed");
        constexpr std::size_t kCompilerStack = 256u * 1024u * 1024u;
        const int stackStatus = pthread_attr_setstacksize(&attr, kCompilerStack);
        if (stackStatus != 0) {
            pthread_attr_destroy(&attr);
            throw std::runtime_error("cannot reserve native compiler stack");
        }

        pthread_t thread;
        const int createStatus = pthread_create(&thread, &attr, compileOnLargeStack, &job);
        pthread_attr_destroy(&attr);
        if (createStatus != 0) throw std::runtime_error("cannot start native compiler thread");
        if (pthread_join(thread, nullptr) != 0) throw std::runtime_error("cannot join native compiler thread");

        if (job.result < 0) {
            std::cerr << "noqeri-native: compile failed with status " << job.result;
            const std::int64_t raw = -job.result;
            if (raw >= 1000000000000000ll) {
                auto payload = raw - 1000000000000000ll;
                const auto kind = payload / 100000000000000ll;
                payload %= 100000000000000ll;
                const auto functionIndex = payload / 1000000000ll;
                payload %= 1000000000ll;
                const auto pc = payload / 64ll;
                const auto opcode = payload % 64ll;
                std::cerr << " kind=" << kind << " function=" << functionIndex
                          << " pc=" << pc << " opcode=" << opcode;
            }
            std::cerr << "\n";
            return 1;
        }
        const auto written = static_cast<std::uint64_t>(job.result);
        if (written > output.size()) throw std::runtime_error("native compiler returned an invalid output size");
        writeFile(argv[2], output.data(), static_cast<std::size_t>(written));
        std::cout << "Wrote native assembly: " << argv[2] << " (" << written << " bytes)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "noqeri-native: " << error.what() << "\n";
        return 1;
    }
}
