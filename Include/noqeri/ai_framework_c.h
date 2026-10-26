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

// Self-contained differentiable graph. Every request owns its graph and
// all input/output storage; no global handles or retained caller pointers.
// Operations: 0 leaf; 1 add; 2 matmul; 3 transpose; 4 scale;
// 5 tanh; 6 softmax (target=1 for causal); 7 cross-entropy;
// 8 last row. Operands refer only to earlier step indices.
typedef struct nq_ai_tape_step {
    uint32_t operation;
    size_t a, b;
    const double *data;
    size_t rows, cols;
    double scalar;
    size_t target;
} nq_ai_tape_step;

typedef struct nq_ai_tape_request {
    const nq_ai_tape_step *steps;
    size_t step_count;
    size_t output_step;
    size_t loss_step;
    size_t gradient_step;
    uint32_t backward;
    uint32_t backend; /* scalar=0, tiled=1, SIMD=2, auto=3 */
    double *output;
    size_t output_capacity;
    double *gradient;
    size_t gradient_capacity;
    size_t *output_rows;
    size_t *output_cols;
} nq_ai_tape_request;
// 0 success; -1 invalid required pointer; -2 graph shape/budget;
// -3 insufficient output storage; -4 invalid graph/gradient; -5 SDK failure.
// On any nonzero return, the output, gradient and shape fields are unchanged.
int64_t nq_ai_tape_execute(const nq_ai_tape_request *request);

typedef struct nq_ai_bpe_request {
    const uint8_t *corpus;
    size_t corpus_length;
    const uint8_t *text;
    size_t text_length;
    size_t max_merges;
    size_t *tokens;
    size_t token_capacity;
    size_t *token_count;
} nq_ai_bpe_request;
// 0 success; -1 null pointer; -2 bounds; -3 capacity; -5 SDK/input.
int64_t nq_ai_bpe_encode(const nq_ai_bpe_request *request);

typedef struct nq_ai_transformer_request {
    const uint8_t *corpus;
    size_t corpus_length;
    const uint8_t *prompt;
    size_t prompt_length;
    size_t context, hidden, steps, merges, output_tokens;
    double learning_rate;
    uint32_t seed, backend;
    uint8_t *output;
    size_t output_capacity;
    size_t *output_length;
    double *training_loss;
} nq_ai_transformer_request;
// Fresh transient model: no retained caller pointers, network or file access.
int64_t nq_ai_transformer_train_generate(const nq_ai_transformer_request *request);
#ifdef __cplusplus
}
#endif
#endif
