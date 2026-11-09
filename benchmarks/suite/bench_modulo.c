#include <stdio.h>
#include <stdint.h>
#include <time.h>

int64_t bench_modulo(int64_t iterations) {
    int64_t sum = 0;
    for (int64_t i = 1; i <= iterations; ++i) {
        int64_t m = (i * 37 + 13) % 997;
        sum += m;
    }
    return sum;
}

int main(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int64_t res = bench_modulo(10000000);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    printf("C\tmodulo\t%lld\t%.6f\n", (long long)res, elapsed);
    return 0;
}
