// SPDX-License-Identifier: Apache-2.0
#ifndef NOQERI_AI_FRAMEWORK_C_H
#define NOQERI_AI_FRAMEWORK_C_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Stable, single-pointer ABI for Noqeri native AOT and C/C++ consumers.
// f64 tensors are row-major. Caller retains ownership and must supply valid,
// writable aligned buffers; no function holds onto caller-owned pointers.
typedef struct nq_ai_gemm_request {
    const double *left;
    size_t left_rows;
    size_t left_cols;
    const double *right;
    size_t right_rows;
    size_t right_cols;
    double *output;
    size_t output_capacity;
    uint32_t backend; // 0 scalar, 1 tiled, 2 SIMD, 3 auto
} nq_ai_gemm_request;
// 0 success, negatives for invalid args/shape/capacity/backend/runtime failure.
// Failures do not modify the output buffer. No C++ exception crosses this ABI.
int64_t nq_ai_gemm(const nq_ai_gemm_request *request);
#ifdef __cplusplus
}
#endif
#endif
