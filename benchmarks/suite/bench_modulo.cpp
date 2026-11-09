#include <iostream>
#include <cstdint>
#include <chrono>

int64_t bench_modulo(int64_t iterations) {
    int64_t sum = 0;
    for (int64_t i = 1; i <= iterations; ++i) {
        int64_t m = (i * 37 + 13) % 997;
        sum += m;
    }
    return sum;
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    int64_t res = bench_modulo(10000000);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "C++\tmodulo\t" << res << "\t" << elapsed.count() << "\n";
    return 0;
}
