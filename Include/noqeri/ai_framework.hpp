// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noqeri/lm.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace noqeri_ai::framework {
enum class Backend { Scalar, Tiled, Simd, Auto, OpenCL }; // OpenCL requires real GPU device
// FP16/FP32 compute uses float accumulation, FP64 uses double.
enum class Precision { FP64, FP32, FP16 };
bool gpu_available() noexcept;
// Describes the selected CL_DEVICE_TYPE_GPU; throws if no GPU was found.
// Hardware provenance, not a cryptographic device attestation.
std::string gpu_device_description();
Matrix gpu_gemm(const Matrix& a,const Matrix& b);
bool simd_available() noexcept;
Matrix gemm(const Matrix& a, const Matrix& b, Backend backend = Backend::Auto);

// Reverse-mode automatic differentiation for small, bounded dense tensors.
// Nodes are referenced by integer handles; no user-provided callbacks run in backward().
class Tape {
public:
    void set_backend(Backend backend);
    void set_precision(Precision precision);
    std::size_t leaf(const Matrix& value);
    std::size_t add(std::size_t a, std::size_t b);
    std::size_t matmul(std::size_t a, std::size_t b);
    std::size_t transpose(std::size_t a);
    std::size_t scale(std::size_t a, double factor);
    std::size_t tanh(std::size_t a);
    std::size_t relu(std::size_t a);
    std::size_t gelu(std::size_t a);
    std::size_t layer_norm(std::size_t a,double epsilon = 1e-5);
    std::size_t softmax(std::size_t a, bool causal = false);
    std::size_t gather_rows(std::size_t embedding, const std::vector<std::size_t>& indices);
    std::size_t last_row(std::size_t a);
    std::size_t cross_entropy(std::size_t logits, std::size_t target);
    const Matrix& value(std::size_t id) const;
    const Matrix& grad(std::size_t id) const;
    void backward(std::size_t loss);
    std::size_t size() const noexcept;
private:
    enum class Op { Leaf, Add, Matmul, Transpose, Scale, Tanh, Softmax, Gather, LastRow, CrossEntropy, Relu, Gelu, LayerNorm };
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
    Precision precision_ = Precision::FP64;
    std::size_t append(Node node);
};
void sgd_update(Matrix& parameter, const Matrix& gradient,
                double learning_rate, double gradient_clip = 1.0);

// The bounded AdamW optimizer uses decoupled weight decay and bias-corrected
// first/second moments. Each step is atomic across all parameter groups.
struct AdamWOptions {
    double learning_rate = 0.001;
    double beta1 = 0.9;
    double beta2 = 0.999;
    double epsilon = 1e-8;
    double weight_decay = 0.01;
};
struct AdamWState {
    std::vector<Matrix> first, second;
    std::size_t steps = 0;
};
class AdamWOptimizer {
public:
    explicit AdamWOptimizer(const std::vector<Matrix*>& parameters);
    void step(const std::vector<Matrix*>& parameters,
              const std::vector<const Matrix*>& gradients,
              const AdamWOptions& options = {});
    std::size_t steps() const noexcept;
    AdamWState state() const;
    void restore(const AdamWState& state);
private:
    std::vector<Matrix> first_, second_;
    std::size_t steps_ = 0;
};
// Global L2 norm treats all parameter gradients as a single flat vector.
// Clipping validates and stages all tensors before committing any mutations.
double global_gradient_norm(const std::vector<const Matrix*>& gradients);
double clip_grad_norm(std::vector<Matrix*>& gradients,double max_norm);
// Warmup followed by an inclusive cosine decay to min_rate.
double cosine_learning_rate(std::size_t step,std::size_t total_steps,
                            double peak,double min_rate=0,
                            std::size_t warmup=0);

struct SubwordTokenizer {
    // 0 is LF; 1..95 are ASCII 32..126; merges extend the vocabulary.
    std::vector<std::pair<std::size_t,std::size_t>> merges;
    std::size_t vocab_size() const noexcept { return 96+merges.size(); }
    std::vector<std::size_t> encode(const std::string& text) const;
    std::string decode(const std::vector<std::size_t>& tokens) const;
    static SubwordTokenizer train(const std::string& corpus,
                                  std::size_t max_merges = 16);
};

enum class TrainingOptimizer { SGD, AdamW };
struct TransformerOptions {
    std::size_t context = 4; // 1..16
    std::size_t hidden = 8;  // 2..24
    std::size_t steps = 200; // 1..10000 optimizer updates
    std::size_t merges = 8;  // 0..32
    double learning_rate = 0.025;
    std::uint32_t seed = 42;
    Backend backend = Backend::Auto;
    TrainingOptimizer optimizer = TrainingOptimizer::SGD;
    std::size_t batch_size = 1; // 1..16 independent training samples/step
    double gradient_clip_norm = 1.0; // maximum global L2 norm
    double weight_decay = 0.01; // AdamW only, decoupled from gradients
    std::size_t warmup_steps = 0; // <= steps, zero disables warmup
    bool cosine_decay = false;
    std::size_t layers = 1; // 1..4 causal attention blocks
    Precision precision = Precision::FP64;
    std::string checkpoint_path; // optional .nqck, atomic checkpoint writes
    std::string resume_path; // resume only matching corpus and options
    std::size_t stop_after = 0; // 0=finish, otherwise stop after this many total updates
    double minimum_learning_rate = 1e-6; // cosine floor
    double validation_fraction = 0.0; // [0,0.3], optional holdout
};
struct TransformerTrainingReport {
    double train_loss = 0.0; // trailing training loss
    double validation_loss = 0.0; // exact 0 when validation disabled
    double validation_perplexity = 0.0;
    std::size_t validation_positions = 0;
    std::size_t optimizer_steps = 0;
    bool completed = true;
};
struct TransformerEvaluation {
    double loss = 0.0;
    double perplexity = 0.0;
    std::size_t positions = 0;
};
struct TransformerModel {
    SubwordTokenizer tokenizer;
    std::size_t context = 0, hidden = 0;
    // First layer fields retained for backwards-compatible C++ consumers.
    Matrix embedding, wq, wk, wv, wo, w1, w2, output;
    struct Block { Matrix wq,wk,wv,wo,w1,w2; };
    std::vector<Block> extra_layers; // layers 2..4
};
TransformerModel train_transformer(const std::string& corpus,
                                    const TransformerOptions& options,
                                    double* training_loss = nullptr,
                                    TransformerTrainingReport* report = nullptr);
// Deterministic, bounded teacher-forced next-token evaluation (max 256 positions).
TransformerEvaluation evaluate_transformer(const TransformerModel& model,
                                            const std::string& text,
                                            Backend backend = Backend::Auto);
std::string generate_transformer(const TransformerModel& model,
                                 const std::string& prompt,
                                 std::size_t tokens = 24,
                                 double temperature = 0.0,
                                 std::uint32_t seed = 42,
                                 Backend backend = Backend::Auto);
void save_transformer(const TransformerModel& model, const std::string& path);
TransformerModel load_transformer(const std::string& path);
} // namespace noqeri_ai::framework
