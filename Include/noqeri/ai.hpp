// SPDX-License-Identifier: Apache-2.0
#pragma once
// Small on-CPU binary classifier and capability-bounded agent loop.
// No remote calls, hidden external tools, accelerators or training framework.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace noqeri_ai {
struct Sample {
    std::vector<double> features;
    double label = 0; // train: 0 or 1; predict: ignored
};
struct Options {
    unsigned epochs = 160;
    double learning_rate = 0.08;
    double l2 = 0.0002;
    std::uint32_t seed = 42;
};
struct Model {
    std::vector<double> mean;
    std::vector<double> scale;
    std::vector<double> weights;
    double bias = 0.0;
};
struct TrainingReport {
    std::size_t samples = 0;
    std::size_t train_samples = 0;
    std::size_t validation_samples = 0;
    double train_loss = 0.0;
    double validation_loss = 0.0;
    double validation_accuracy = 0.0;
};
struct AgentPolicy {
    std::size_t max_steps = 100;
    std::size_t max_tool_calls = 100;
    bool allow_emit = false;
};
struct AgentReport {
    std::size_t observations = 0;
    std::size_t steps = 0;
    std::size_t tool_calls = 0;
    std::size_t denied_calls = 0;
    bool budget_exhausted = false;
};
using ToolCallback = std::function<bool(const std::string& tool,
                                         std::size_t observation_index,
                                         double probability)>;

std::vector<Sample> read_csv(const std::string& filename, bool has_label,
                             std::size_t max_rows = 100000);
Model train(const std::vector<Sample>& samples, const Options& options,
            TrainingReport* report = nullptr);
double predict_probability(const Model& model, const std::vector<double>& features);
void save_model(const Model& model, const std::string& filename);
Model load_model(const std::string& filename);
AgentReport run_agent(const Model& model,
                      const std::vector<Sample>& observations,
                      const AgentPolicy& policy, const ToolCallback& callback);
}
