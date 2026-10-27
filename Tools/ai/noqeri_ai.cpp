// SPDX-License-Identifier: GPL-3.0-only
// Native Noqeri AI development host. The reusable runtime library is Apache-2.0.
#include "noqeri/ai.hpp"
#include "noqeri/lm.hpp"
#include "noqeri/ai_framework.hpp"
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
std::size_t nonnegative(const std::string& value,std::size_t max) {
    if(value.empty()||value.size()>15)throw std::runtime_error("invalid nonnegative integer");
    for(char ch:value)if(ch<'0'||ch>'9')
        throw std::runtime_error("invalid integer option");
    const auto number=std::stoull(value);
    if(number>max)throw std::runtime_error("integer outside allowed budget");
    return static_cast<std::size_t>(number);
}
double decimal(const std::string& value) {
    std::size_t end=0;
    const auto number=std::stod(value,&end);
    if(end!=value.size()||!std::isfinite(number))throw std::runtime_error("nonfinite numeric option");
    return number;
}
noqeri_ai::framework::Backend backend_name(const std::string& name) {
    using noqeri_ai::framework::Backend;
    if(name=="auto")return Backend::Auto;
    if(name=="scalar")return Backend::Scalar;
    if(name=="tiled")return Backend::Tiled;
    if(name=="simd")return Backend::Simd;
    if(name=="opencl")return Backend::OpenCL;
    throw std::runtime_error("invalid tensor backend: "+name);
}
void help() {
    std::cout <<
      "noqeri-ai 0.1.4 (offline CPU-only patch)\n"
      "  train <numeric-training.csv> <model.nqai> [--epochs N] [--rate LR] [--l2 L2] [--seed N]\n"
      "  predict <model.nqai> <feature1> [feature2 ...]\n"
      "  agent <model.nqai> <numeric-observations.csv> [--max-steps N] [--allow-emit]\n"
      "  lm train <ascii-corpus.txt> <model.nqlm> [--steps N] [--context N] [--hidden N] [--rate LR] [--seed N]\n"
      "  lm generate <model.nqlm> <prompt> [--tokens N] [--temperature X] [--seed N]\n"
      "  transformer train <text.txt> <model.nqtf> [--steps N] [--context N] [--hidden N] [--merges N]\n"
      "    [--optimizer sgd|adamw] [--batch N] [--rate LR] [--clip N] [--weight-decay N]\n"
      "    [--schedule constant|cosine] [--warmup N] [--min-rate N] [--validation N]\n"
      "    [--layers N] [--precision fp64|fp32|fp16] [--backend auto|scalar|tiled|simd|opencl]\n"
      "    [--checkpoint FILE] [--resume FILE] [--stop-after N] (restartable training)\n"
      "  transformer evaluate <model.nqtf> <text.txt> [--backend auto|scalar|tiled|simd|opencl]\n"
      "  transformer generate <model.nqtf> <prompt> [--tokens N] [--temperature X] [--seed N] [--backend auto|scalar|tiled|simd]\n"
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

        if(command=="transformer") {
            using namespace noqeri_ai::framework;
            if(argc<4)throw std::runtime_error("usage: transformer train|generate ...");
            const std::string action=argv[2];
            if(action=="train") {
                if(argc<5)throw std::runtime_error("usage: transformer train <corpus> <model.nqtf>");
                TransformerOptions options;
                for(int i=5;i<argc;++i) {
                    const std::string flag=argv[i];
                    if(i+1>=argc)throw std::runtime_error("missing transformer train option");
                    const std::string value=argv[++i];
                    if(flag=="--steps")options.steps=natural(value,10000);
                    else if(flag=="--context")options.context=natural(value,16);
                    else if(flag=="--hidden")options.hidden=natural(value,24);
                    else if(flag=="--merges")options.merges=nonnegative(value,32);
                    else if(flag=="--rate")options.learning_rate=decimal(value);
                    else if(flag=="--seed")options.seed=static_cast<std::uint32_t>(nonnegative(value,4294967295ULL));
                    else if(flag=="--backend")options.backend=backend_name(value);
                    else if(flag=="--optimizer") {
                        if(value=="sgd")options.optimizer=TrainingOptimizer::SGD;
                        else if(value=="adamw")options.optimizer=TrainingOptimizer::AdamW;
                        else throw std::runtime_error("optimizer must be sgd or adamw");
                    }
                    else if(flag=="--batch")options.batch_size=natural(value,16);
                    else if(flag=="--clip")options.gradient_clip_norm=decimal(value);
                    else if(flag=="--weight-decay")options.weight_decay=decimal(value);
                    else if(flag=="--schedule") {
                        if(value=="cosine")options.cosine_decay=true;
                        else if(value=="constant")options.cosine_decay=false;
                        else throw std::runtime_error("schedule must be constant or cosine");
                    }
                    else if(flag=="--warmup")options.warmup_steps=nonnegative(value,9999);
                    else if(flag=="--min-rate")options.minimum_learning_rate=decimal(value);
                    else if(flag=="--validation")options.validation_fraction=decimal(value);
                    else if(flag=="--layers")options.layers=natural(value,4);
                    else if(flag=="--precision") {
                        if(value=="fp64")options.precision=Precision::FP64;
                        else if(value=="fp32")options.precision=Precision::FP32;
                        else if(value=="fp16")options.precision=Precision::FP16;
                        else throw std::runtime_error("precision must be fp64, fp32 or fp16");
                    }
                    else if(flag=="--checkpoint")options.checkpoint_path=value;
                    else if(flag=="--resume")options.resume_path=value;
                    else if(flag=="--stop-after")options.stop_after=natural(value,10000);
                    else throw std::runtime_error("unknown transformer train flag "+flag);
                }
                const auto corpus=noqeri_ai::read_language_corpus(argv[3]);
                double training_loss=0;
                TransformerTrainingReport report;
                auto model=train_transformer(corpus,options,&training_loss,&report);
                save_transformer(model,argv[4]);
                std::cout<<std::setprecision(9)
                         <<"transformer_tokens="<<model.tokenizer.encode(corpus).size()
                         <<" vocab="<<model.tokenizer.vocab_size()
                         <<" steps="<<report.optimizer_steps
                         <<" completed="<<(report.completed?1:0)
                         <<" layers="<<model.extra_layers.size()+1
                         <<" precision="<<(options.precision==Precision::FP16?"fp16":
                                              options.precision==Precision::FP32?"fp32":"fp64")
                         <<" batch="<<options.batch_size
                         <<" optimizer="<<(options.optimizer==TrainingOptimizer::AdamW?"adamw":"sgd")
                         <<" trailing_loss="<<training_loss;
                if(report.validation_positions)
                    std::cout<<" validation_loss="<<report.validation_loss
                             <<" validation_perplexity="<<report.validation_perplexity
                             <<" validation_positions="<<report.validation_positions;
                std::cout<<"\n";
                return 0;
            }
            if(action=="evaluate") {
                if(argc<5)throw std::runtime_error("usage: transformer evaluate <model.nqtf> <corpus.txt>");
                Backend backend=Backend::Auto;
                for(int i=5;i<argc;++i) {
                    const std::string flag=argv[i];
                    if(i+1>=argc)throw std::runtime_error("missing transformer evaluate option");
                    const std::string value=argv[++i];
                    if(flag=="--backend")backend=backend_name(value);
                    else throw std::runtime_error("unknown transformer evaluate flag "+flag);
                }
                const auto model=load_transformer(argv[3]);
                const auto corpus=noqeri_ai::read_language_corpus(argv[4]);
                const auto evaluation=evaluate_transformer(model,corpus,backend);
                std::cout<<std::setprecision(9)
                         <<"eval_loss="<<evaluation.loss
                         <<" perplexity="<<evaluation.perplexity
                         <<" positions="<<evaluation.positions<<"\n";
                return 0;
            }
            if(action=="generate") {
                if(argc<5)throw std::runtime_error("usage: transformer generate <model.nqtf> <prompt>");
                std::size_t count=24;
                double temperature=0.0;
                std::uint32_t seed=42;
                Backend backend=Backend::Auto;
                for(int i=5;i<argc;++i) {
                    const std::string flag=argv[i];
                    if(i+1>=argc)throw std::runtime_error("missing transformer generate option");
                    const std::string value=argv[++i];
                    if(flag=="--tokens")count=natural(value,64);
                    else if(flag=="--temperature")temperature=decimal(value);
                    else if(flag=="--seed")seed=static_cast<std::uint32_t>(natural(value,4294967295ULL));
                    else if(flag=="--backend")backend=backend_name(value);
                    else throw std::runtime_error("unknown transformer generate flag "+flag);
                }
                const auto model=load_transformer(argv[3]);
                const auto output=generate_transformer(model,argv[4],count,temperature,seed,backend);
                std::cout<<"generated="<<output<<"\n";
                return 0;
            }
            throw std::runtime_error("unknown transformer command: "+action);
        }
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
