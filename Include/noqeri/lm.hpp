// SPDX-License-Identifier: Apache-2.0
#pragma once
// Optional, dependency-free CPU numerical / small autoregressive language-model SDK.
// Ordinary C++ library routines; no special language syntax or hidden tool access.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace noqeri_ai {
struct Matrix {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> values;
    Matrix() = default;
    Matrix(std::size_t r, std::size_t c, double fill = 0.0);
    double& at(std::size_t r, std::size_t c);
    double at(std::size_t r, std::size_t c) const;
};
Matrix matmul(const Matrix& left, const Matrix& right);
Matrix softmax_rows(const Matrix& input);
Matrix rms_norm_rows(const Matrix& input, double epsilon = 1e-5);
// Causal single-head scaled dot-product attention, no trainable projections.
Matrix causal_attention(const Matrix& query, const Matrix& key, const Matrix& value);

struct LanguageOptions {
    std::size_t context = 8;
    std::size_t hidden = 24;
    std::size_t steps = 1500;
    double learning_rate = 0.05;
    std::uint32_t seed = 42;
};
struct LanguageModel {
    std::size_t context = 0;
    std::size_t hidden = 0;
    // Fixed printable ASCII plus newline vocabulary (96 tokens).
    std::vector<double> embedding; // [96, hidden]
    std::vector<double> projection; // [hidden, 96]
    std::vector<double> bias;       // [96]
};
// Train a tiny context-averaged neural next-character predictor.
// Useful for experimentation, NOT a transformer / general LLM trainer.
LanguageModel train_language(const std::string& corpus, const LanguageOptions& options,
                             double* trailing_loss = nullptr);
std::string generate_language(const LanguageModel& model, const std::string& prompt,
                              std::size_t max_tokens = 64,
                              double temperature = 0.0, std::uint32_t seed = 42);
void save_language(const LanguageModel& model, const std::string& filename);
LanguageModel load_language(const std::string& filename);
std::string read_language_corpus(const std::string& filename);
} // namespace noqeri_ai
