#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define N 256
static int64_t A[N * N];
static int64_t B[N * N];
static int64_t C[N * N];

void bench_matrix(void) {
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

int main(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    bench_matrix();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    printf("C\tmatrix\t%lld\t%.6f\n", (long long)C[N*N-1], elapsed);
    return 0;
}
