#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define N 2000
static int64_t arr[N];

void bench_sort(void) {
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

int main(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    bench_sort();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    printf("C\tsorting\t%lld\t%.6f\n", (long long)arr[N-1], elapsed);
    return 0;
}
