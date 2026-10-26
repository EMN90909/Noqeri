// SPDX-License-Identifier: GPL-3.0-only
// Native Noqeri AI development host. The reusable runtime library is Apache-2.0.
#include "noqeri/ai.hpp"
#include "noqeri/lm.hpp"
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t natural(const std::string& value,std::size_t max) {
    if(value.empty()||value.size()>15)throw std::runtime_error("invalid positive integer");
    for(char ch:value)if(ch<'0'||ch>'9')throw std::runtime_error("invalid integer option");
    const auto number=std::stoull(value);
    if(number==0||number>max)throw std::runtime_error("positive integer outside allowed budget");
    return static_cast<std::size_t>(number);
}
double decimal(const std::string& value) {
    std::size_t end=0;
    const auto number=std::stod(value,&end);
    if(end!=value.size()||!std::isfinite(number))throw std::runtime_error("nonfinite numeric option");
    return number;
}
void help() {
    std::cout <<
      "noqeri-ai 0.1.4 (offline CPU-only patch)\n"
      "  train <numeric-training.csv> <model.nqai> [--epochs N] [--rate LR] [--l2 L2] [--seed N]\n"
      "  predict <model.nqai> <feature1> [feature2 ...]\n"
      "  agent <model.nqai> <numeric-observations.csv> [--max-steps N] [--allow-emit]\n"
      "  lm train <ascii-corpus.txt> <model.nqlm> [--steps N] [--context N] [--hidden N] [--rate LR] [--seed N]\n"
      "  lm generate <model.nqlm> <prompt> [--tokens N] [--temperature X] [--seed N]\n"
      "  --version\n"
      "Training CSV has numeric features then a binary 0/1 label, without a header.\n"
      "Agent tools are default-denied; --allow-emit permits stdout only.\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&(std::string(argv[1])=="--version"||std::string(argv[1])=="version")) {
            std::cout<<"noqeri-ai 0.1.4-patch\n";return 0;
        }
        if(argc<2) {help();return 2;}
        const std::string command=argv[1];
        if(command=="lm") {
            if(argc<4)throw std::runtime_error("usage: lm train|generate ...");
            const std::string task=argv[2];
            if(task=="train") {
                if(argc<5)throw std::runtime_error("usage: lm train <corpus.txt> <model.nqlm>");
                noqeri_ai::LanguageOptions options;
                for(int i=5;i<argc;++i) {
                    const std::string flag=argv[i];
                    if(i+1>=argc)throw std::runtime_error("missing lm train option value");
                    const std::string value=argv[++i];
                    if(flag=="--steps")options.steps=natural(value,200000);
                    else if(flag=="--context")options.context=natural(value,64);
                    else if(flag=="--hidden")options.hidden=natural(value,64);
                    else if(flag=="--rate")options.learning_rate=decimal(value);
                    else if(flag=="--seed")options.seed=static_cast<std::uint32_t>(natural(value,4294967295ULL));
                    else throw std::runtime_error("unknown lm train flag "+flag);
                }
                const auto corpus=noqeri_ai::read_language_corpus(argv[3]);
                double loss=0.0;
                const auto model=noqeri_ai::train_language(corpus,options,&loss);
                noqeri_ai::save_language(model,argv[4]);
                std::cout<<std::setprecision(8)<<"lm_trained_bytes="<<corpus.size()
                         <<" steps="<<options.steps<<" context="<<options.context
                         <<" hidden="<<options.hidden<<" trailing_loss="<<loss<<"\n";
                return 0;
            }
            if(task=="generate") {
                if(argc<5)throw std::runtime_error("usage: lm generate <model.nqlm> <prompt>");
                std::size_t tokens=64;
                double temperature=0.0;
                std::uint32_t seed=42;
                for(int i=5;i<argc;++i) {
                    const std::string flag=argv[i];
                    if(i+1>=argc)throw std::runtime_error("missing lm generate option value");
                    const std::string value=argv[++i];
                    if(flag=="--tokens")tokens=natural(value,256);
                    else if(flag=="--temperature")temperature=decimal(value);
                    else if(flag=="--seed")seed=static_cast<std::uint32_t>(natural(value,4294967295ULL));
                    else throw std::runtime_error("unknown lm generate flag "+flag);
                }
                const auto model=noqeri_ai::load_language(argv[3]);
                const auto result=noqeri_ai::generate_language(model,argv[4],tokens,temperature,seed);
                std::cout<<"generated="<<result<<"\n";
                return 0;
            }
            throw std::runtime_error("unknown lm subcommand: "+task);
        }
        if(command=="train") {
            if(argc<4)throw std::runtime_error("usage: train <data.csv> <output.nqai>");
            noqeri_ai::Options opts;
            for(int i=4;i<argc;++i) {
                const std::string flag=argv[i];
                if(i+1>=argc)throw std::runtime_error("missing value for "+flag);
                const std::string value=argv[++i];
                if(flag=="--epochs")opts.epochs=static_cast<unsigned>(natural(value,10000));
                else if(flag=="--rate")opts.learning_rate=decimal(value);
                else if(flag=="--l2")opts.l2=decimal(value);
                else if(flag=="--seed")opts.seed=static_cast<std::uint32_t>(natural(value,4294967295ULL));
                else throw std::runtime_error("unknown training flag "+flag);
            }
            const auto samples=noqeri_ai::read_csv(argv[2],true);
            noqeri_ai::TrainingReport report;
            const auto model=noqeri_ai::train(samples,opts,&report);
            noqeri_ai::save_model(model,argv[3]);
            std::cout<<std::setprecision(8)<<"trained rows="<<report.samples
                <<" train="<<report.train_samples<<" validation="<<report.validation_samples
                <<" train_loss="<<report.train_loss
                <<" validation_loss="<<report.validation_loss
                <<" validation_accuracy="<<report.validation_accuracy
                <<" dimensions="<<model.weights.size()<<"\n";
            return 0;
        }
        if(command=="predict") {
            if(argc<4)throw std::runtime_error("usage: predict <model> <features...>");
            auto model=noqeri_ai::load_model(argv[2]);
            std::vector<double> features;
            for(int i=3;i<argc;++i)features.push_back(decimal(argv[i]));
            const auto value=noqeri_ai::predict_probability(model,features);
            std::cout<<std::setprecision(10)<<"probability="<<value
                     <<" class="<<(value>=0.5?1:0)<<"\n";
            return 0;
        }
        if(command=="agent") {
            if(argc<4)throw std::runtime_error("usage: agent <model> <observations.csv>");
            const auto model=noqeri_ai::load_model(argv[2]);
            auto observations=noqeri_ai::read_csv(argv[3],false);
            noqeri_ai::AgentPolicy policy;
            for(int i=4;i<argc;++i) {
                const std::string flag=argv[i];
                if(flag=="--allow-emit")policy.allow_emit=true;
                else if(flag=="--max-steps") {
                    if(i+1>=argc)throw std::runtime_error("missing step budget");
                    policy.max_steps=natural(argv[++i],10000);
                }
                else throw std::runtime_error("unknown agent option "+flag);
            }
            policy.max_tool_calls=policy.max_steps;
            const auto result=noqeri_ai::run_agent(model,observations,policy,
                [](const std::string& tool,std::size_t index,double probability) {
                    std::cout<<std::setprecision(9)<<"step="<<index+1
                             <<" tool="<<tool<<" probability="<<probability<<"\n";
                    return true;
                });
            std::cout<<"agent_steps="<<result.steps<<" tools="<<result.tool_calls
                     <<" denied="<<result.denied_calls
                     <<" budget_exhausted="<<(result.budget_exhausted?1:0)<<"\n";
            return 0;
        }
        help();return 2;
    }catch(const std::exception& e) {
        std::cerr<<"NQR-AI1001: "<<e.what()<<"\n";
        return 1;
    }
}
