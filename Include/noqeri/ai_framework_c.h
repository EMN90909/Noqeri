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

typedef struct nq_ai_softmax_request {
    const double *input;
    size_t rows, cols;
    double *output;
    size_t output_capacity;
    uint32_t causal;
} nq_ai_softmax_request;
int64_t nq_ai_softmax(const nq_ai_softmax_request *request);
// 0 success; negatives: pointer -1, shape -2, capacity -3, mode -4, runtime -5.
typedef struct nq_ai_sgd_request {
    const double *parameters, *gradients;
    size_t count;
    double learning_rate, gradient_clip;
    double *output;
    size_t output_capacity;
} nq_ai_sgd_request;
int64_t nq_ai_sgd(const nq_ai_sgd_request *request);
#ifdef __cplusplus
}
#endif
#endif
