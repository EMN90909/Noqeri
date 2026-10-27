// SPDX-License-Identifier: Apache-2.0
#ifndef NOQERI_AI_MATRIX_C_H
#define NOQERI_AI_MATRIX_C_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Version 1 of the single-request matrix C ABI. Row-major f64, caller-owned
// storage. Inputs may overlap the output: implementations stage all results.
// The caller must supply properly aligned, accessible buffers of the declared
// sizes. No pointers are retained and no C++ exceptions cross the boundary.
//
// operation:
//  0 GEMM,  1 transpose, 2 softmax, 3 log_softmax, 4 logsumexp,
//  5 layer_norm_rows, 6 GELU, 7 softplus, 8 solve, 9 inverse,
// 10 cholesky, 11 covariance_columns, 12 maximum, 13 minimum,
// 14 add, 15 subtract, 16 multiply, 17 divide, 18 sum,
// 19 mean, 20 variance, 21 normalize_columns, 22 conv2d_valid,
// 23 trace, 24 l1_norm.
// Binary operations 0, 8, 14-17, 22 require right and its dimensions.
// axis (0 or 1) is used for 2-4, 12-13, 18-20. For 22, axis is
// the positive convolution stride. epsilon is used for 5 and 21.
// Other parameters are ignored.
// Result dimensions may be smaller/different than the input dimensions.
typedef struct nq_ai_matrix_request {
    const double *left;
    size_t left_rows, left_cols;
    const double *right;
    size_t right_rows, right_cols;
    uint32_t operation;
    size_t axis;
    double epsilon;
    double *output;
    size_t output_capacity;
    size_t *output_rows, *output_cols;
} nq_ai_matrix_request;

// 0 success; -1 missing pointer, -2 invalid input shape, -3 capacity,
// -4 unknown operation/invalid axis, -5 operation/value/numerical failure.
// All outputs (including shape) are unchanged on failure.
int64_t nq_ai_matrix_execute(const nq_ai_matrix_request *request);

#ifdef __cplusplus
}
#endif
#endif
