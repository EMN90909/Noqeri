#include <iostream>
#include <cstdint>
#include <vector>
#include <chrono>

constexpr int N = 256;
static int64_t A[N * N];
static int64_t B[N * N];
static int64_t C[N * N];

void bench_matrix() {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            A[i * N + j] = (i + j) % 100;
            B[i * N + j] = (i * j + 1) % 100;
            C[i * N + j] = 0;
        }
    }
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < N; ++k) {
            int64_t r = A[i * N + k];
            for (int j = 0; j < N; ++j) {
                C[i * N + j] += r * B[k * N + j];
            }
        }
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    bench_matrix();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "C++\tmatrix\t" << C[N*N-1] << "\t" << elapsed.count() << "\n";
    return 0;
}
