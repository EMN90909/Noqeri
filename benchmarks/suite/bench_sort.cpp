#include <iostream>
#include <cstdint>
#include <chrono>

constexpr int N = 2000;
static int64_t arr[N];

void bench_sort() {
    int64_t seed = 12345;
    for (int i = 0; i < N; ++i) {
        seed = (seed * 1103515245 + 12345) % 2147483648ULL;
        arr[i] = seed % 100000;
    }
    for (int j = 1; j < N; ++j) {
        int64_t key = arr[j];
        int k = j - 1;
        while (k >= 0 && arr[k] > key) {
            arr[k + 1] = arr[k];
            k--;
        }
        arr[k + 1] = key;
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    bench_sort();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "C++\tsorting\t" << arr[N-1] << "\t" << elapsed.count() << "\n";
    return 0;
}
