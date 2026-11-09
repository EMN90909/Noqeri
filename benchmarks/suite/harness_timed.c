#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include "abi.h"

extern int64_t noqeri_entry(noqeri_abi* abi);

static char out_buf[128];
static size_t out_len = 0;

static int32_t write_output(void* ctx, const char* data, size_t size) {
    if (!data || size == 0) return 0;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n' || data[i] == '\r') continue;
        if (out_len + 1 < sizeof(out_buf)) {
            out_buf[out_len++] = data[i];
        }
    }
    out_buf[out_len] = '\0';
    return 0;
}

int main(void) {
    noqeri_abi abi = {0};
    abi.abi_version = NOQERI_ABI_VERSION;
    abi.struct_size = sizeof(noqeri_abi);
    abi.write = write_output;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int64_t res = noqeri_entry(&abi);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    if (out_len > 0) {
        printf("%s\t%.6f\n", out_buf, elapsed);
    } else {
        printf("%ld\t%.6f\n", res, elapsed);
    }
    return 0;
}
