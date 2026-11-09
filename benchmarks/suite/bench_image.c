#include <stdio.h>
#include <stdint.h>
#include <time.h>

static int64_t pixels[262144];

int main(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int64_t y = 0; y < 512; y++) {
        for (int64_t x = 0; x < 512; x++) {
            int64_t r = (x + y * 3) % 256;
            int64_t g = (x * 7 + y * 13) % 256;
            int64_t b = (x + y) % 256;
            int64_t pixel = (r * 65536) + (g * 256) + b;
            pixels[y * 512 + x] = pixel;
        }
    }

    int64_t checksum = 0;
    for (int64_t i = 0; i < 262144; i++) {
        checksum += pixels[i];
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("C\timage\t%ld\t%.6f\n", checksum, elapsed);
    return 0;
}
