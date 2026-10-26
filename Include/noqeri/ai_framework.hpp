// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noqeri/lm.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace noqeri_ai::framework {
enum class Backend { Scalar, Tiled, Simd, Auto };
bool simd_available() noexcept;
Matrix gemm(const Matrix& a, const Matrix& b, Backend backend = Backend::Auto);

// Reverse-mode automatic differentiation for small, bounded dense tensors.
// Nodes are referenced by integer handles; no user-provided callbacks run in backward().
class Tape {
public:
    void set_backend(Backend backend);
    std::size_t leaf(const Matrix& value);
    std::size_t add(std::size_t a, std::size_t b);
    std::size_t matmul(std::size_t a, std::size_t b);
    std::size_t transpose(std::size_t a);
    std::size_t scale(std::size_t a, double factor);
    std::size_t tanh(std::size_t a);
    std::size_t softmax(std::size_t a, bool causal = false);
    std::size_t gather_rows(std::size_t embedding, const std::vector<std::size_t>& indices);
    std::size_t last_row(std::size_t a);
    std::size_t cross_entropy(std::size_t logits, std::size_t target);
    const Matrix& value(std::size_t id) const;
    const Matrix& grad(std::size_t id) const;
    void backward(std::size_t loss);
    std::size_t size() const noexcept;
private:
    enum class Op { Leaf, Add, Matmul, Transpose, Scale, Tanh, Softmax, Gather, LastRow, CrossEntropy };
    struct Node {
        Matrix value, grad;
        Op op = Op::Leaf;
        std::size_t a = 0, b = 0, target = 0;
        double factor = 1.0;
        bool causal = false;
        std::vector<std::size_t> indices;
    };
    std::vector<Node> nodes_;
    Backend backend_ = Backend::Auto;
    std::size_t append(Node node);
};
void sgd_update(Matrix& parameter, const Matrix& gradient,
                double learning_rate, double gradient_clip = 1.0);

struct SubwordTokenizer {
    // 0 is LF; 1..95 are ASCII 32..126; merges extend the vocabulary.
    std::vector<std::pair<std::size_t,std::size_t>> merges;
    std::size_t vocab_size() const noexcept { return 96+merges.size(); }
    std::vector<std::size_t> encode(const std::string& text) const;
    std::string decode(const std::vector<std::size_t>& tokens) const;
    static SubwordTokenizer train(const std::string& corpus,
                                  std::size_t max_merges = 16);
};

struct TransformerOptions {
    std::size_t context = 4; // 1..16
    std::size_t hidden = 8;  // 2..24
    std::size_t steps = 200; // 1..10000
    std::size_t merges = 8;  // 0..32
    double learning_rate = 0.025;
    std::uint32_t seed = 42;
    Backend backend = Backend::Auto;
};
struct TransformerModel {
    SubwordTokenizer tokenizer;
    std::size_t context = 0, hidden = 0;
    // Single causal self-attention head, residual, one tanh feedforward layer.
    Matrix embedding, wq, wk, wv, wo, w1, w2, output;
};
TransformerModel train_transformer(const std::string& corpus,
                                    const TransformerOptions& options,
                                    double* training_loss = nullptr);
std::string generate_transformer(const TransformerModel& model,
                                 const std::string& prompt,
                                 std::size_t tokens = 24,
                                 double temperature = 0.0,
                                 std::uint32_t seed = 42,
                                 Backend backend = Backend::Auto);
void save_transformer(const TransformerModel& model, const std::string& path);
TransformerModel load_transformer(const std::string& path);
} // namespace noqeri_ai::framework
