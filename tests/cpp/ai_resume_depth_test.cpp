// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/ai_framework.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace noqeri_ai;
using namespace noqeri_ai::framework;
namespace fs=std::filesystem;
template<class F>bool rejects(F f) {try{f();return false;}catch(const std::exception&){return true;}}
bool same(const TransformerModel& a,const TransformerModel& b) {
    if(a.extra_layers.size()!=b.extra_layers.size() ||
       a.output.values!=b.output.values ||
       a.embedding.values!=b.embedding.values)return false;
    for(std::size_t i=0;i<a.extra_layers.size();++i)
        if(a.extra_layers[i].wq.values!=b.extra_layers[i].wq.values ||
           a.extra_layers[i].w1.values!=b.extra_layers[i].w1.values)return false;
    return true;
}
int main(int argc,char** argv) {
    assert(argc==2);
    const auto dir=fs::path(argv[1]);
    fs::create_directories(dir);
    const std::string corpus="abcd abcd abcd\nabcd abcd abcd\nabcd abcd abcd\n"
                             "abcd abcd abcd\nabcd abcd abcd\nabcd abcd abcd\n";
    TransformerOptions options;
    options.steps=24;options.context=4;options.hidden=8;
    options.merges=3;options.layers=3;options.seed=13;
    options.optimizer=TrainingOptimizer::AdamW;options.learning_rate=0.003;
    options.batch_size=2;options.warmup_steps=3;options.cosine_decay=true;
    options.minimum_learning_rate=0.0001;options.validation_fraction=0.2;
    options.precision=Precision::FP32;
    double full_loss=0;
    const auto full=train_transformer(corpus,options,&full_loss);
    assert(full.extra_layers.size()==2);
    const auto old_path=dir/"model-nqtf2.nqtf";
    save_transformer(full,old_path.string());
    const auto restored=load_transformer(old_path.string());
    assert(same(full,restored));
    assert(generate_transformer(restored,"abcd",4)==
           generate_transformer(full,"abcd",4));

    auto paused=options;
    paused.checkpoint_path=(dir/"train.nqck").string();
    paused.stop_after=9;
    TransformerTrainingReport partial;
    const auto early=train_transformer(corpus,paused,nullptr,&partial);
    assert(!partial.completed && partial.optimizer_steps==9);
    assert(fs::file_size(paused.checkpoint_path)>0);
    const auto resumed_options=[&]{
        auto out=options;
        out.resume_path=paused.checkpoint_path;
        out.checkpoint_path=paused.checkpoint_path;
        return out;
    }();
    double resumed_loss=0;
    const auto resumed=train_transformer(corpus,resumed_options,&resumed_loss);
    assert(same(full,resumed));
    assert(full_loss==resumed_loss);
    const auto independent=load_transformer(old_path.string());
    assert(same(full,independent));

    // Fail closed if the resume corpus or optimizer parameters differ.
    auto invalid=resumed_options;
    invalid.learning_rate=0.01;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
    assert(rejects([&]{train_transformer(corpus+"oops",resumed_options);}));
    invalid=resumed_options;
    invalid.layers=4;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
    const auto malformed=dir/"malformed.nqck";
    {
        std::ofstream out(malformed,std::ios::binary);
        out<<"NQCK1 11 12\nbroken";
    }
    invalid=resumed_options;invalid.resume_path=malformed.string();
    assert(rejects([&]{train_transformer(corpus,invalid);}));

    // Legacy one-block .nqtf format still writes and reads NQTF1.
    auto legacy=options;legacy.layers=1;legacy.steps=2;
    legacy.warmup_steps=0;legacy.cosine_decay=false;
    legacy.validation_fraction=0;legacy.precision=Precision::FP64;
    const auto legacy_model=train_transformer(corpus,legacy);
    const auto legacy_path=dir/"legacy.nqtf";
    save_transformer(legacy_model,legacy_path.string());
    std::ifstream file(legacy_path);std::string magic;file>>magic;
    assert(magic=="NQTF1");
    assert(same(load_transformer(legacy_path.string()),legacy_model));

    // FP16 operands with FP32 accumulation and FP64 master weights.
    auto half=legacy;half.layers=2;half.precision=Precision::FP16;
    half.steps=4;half.optimizer=TrainingOptimizer::AdamW;
    half.learning_rate=0.003;
    auto half_model=train_transformer(corpus,half);
    assert(half_model.extra_layers.size()==1);
    assert(!half_model.output.values.empty());
    assert(std::isfinite(evaluate_transformer(half_model,corpus).loss));

    // Selection never silently routes explicit GPU training to CPU.
    Matrix a(2,2,1.0),b(2,2,2.0);
    if(!gpu_available()) {
        assert(rejects([&]{gpu_gemm(a,b);}));
        auto gpu=legacy;
        gpu.backend=Backend::OpenCL;gpu.precision=Precision::FP32;
        assert(rejects([&]{train_transformer(corpus,gpu);}));
    } else {
        const auto result=gpu_gemm(a,b);
        for(double x:result.values)assert(std::abs(x-4.0)<1e-5);
        // Verify GPU forward AND GPU backward matrix multiplications
        // against the scalar gradient reference on physical hardware.
        Matrix inputs(1,2);inputs.values={0.4,-0.3};
        Matrix weights(2,2);weights.values={0.2,0.7,-0.5,0.1};
        Tape physical;physical.set_backend(Backend::OpenCL);
        physical.set_precision(Precision::FP32);
        const auto pa=physical.leaf(inputs),pb=physical.leaf(weights);
        const auto pc=physical.matmul(pa,pb);
        const auto pl=physical.cross_entropy(pc,1);
        physical.backward(pl);
        Tape reference;reference.set_backend(Backend::Scalar);
        const auto ca=reference.leaf(inputs),cb=reference.leaf(weights);
        const auto cc=reference.matmul(ca,cb);
        const auto cl=reference.cross_entropy(cc,1);
        reference.backward(cl);
        assert(std::abs(physical.value(pl).at(0,0)-
                        reference.value(cl).at(0,0))<1e-5);
        for(std::size_t i=0;i<inputs.values.size();++i)
            assert(std::abs(physical.grad(pa).values[i]-
                            reference.grad(ca).values[i])<1e-5);
        for(std::size_t i=0;i<weights.values.size();++i)
            assert(std::abs(physical.grad(pb).values[i]-
                            reference.grad(cb).values[i])<1e-5);
        auto gpu=legacy;gpu.layers=2;
        gpu.backend=Backend::OpenCL;gpu.precision=Precision::FP32;
        gpu.optimizer=TrainingOptimizer::AdamW;gpu.learning_rate=0.003;
        const auto trained=train_transformer(corpus,gpu);
        assert(trained.extra_layers.size()==1);
    }
    std::cout<<"PASS checkpoint exact replay, NQTF1/2, 3-layer training, f16/f32, GPU strict selection";
    if(gpu_available())std::cout<<" (GPU executed)";
    else std::cout<<" (GPU unavailable; fail-closed verified)";
    std::cout<<"\n";
}
