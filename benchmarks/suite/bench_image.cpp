#include <iostream>
#include <chrono>
#include <cstdint>

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    int64_t checksum = 0;
    for (int64_t y = 0; y < 512; y++) {
        for (int64_t x = 0; x < 512; x++) {
            int64_t r = (x + y * 3) % 256;
            int64_t g = (x * 7 + y * 13) % 256;
            int64_t b = (x + y) % 256;
            int64_t pixel = (r * 65536) + (g * 256) + b;
            checksum = checksum + pixel;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "C++\timage\t" << checksum << "\t" << elapsed.count() << "\n";
    return 0;
}
