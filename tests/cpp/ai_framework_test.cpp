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
#include <limits>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using namespace noqeri_ai;
using namespace noqeri_ai::framework;
template<class F> bool rejects(F&& action) {
    try {action();return false;}
    catch(const std::exception&) {return true;}
}
bool near(double a,double b,double tolerance=1e-9) {
    return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));
}
int main(int argc,char** argv) {
    assert(argc==2);
    const fs::path root=argv[1];fs::create_directories(root);
    Matrix a(3,4),b(4,5);
    for(std::size_t i=0;i<a.values.size();++i)a.values[i]=double(int(i%5)-2)*0.03;
    for(std::size_t i=0;i<b.values.size();++i)b.values[i]=double(int(i%7)-3)*0.04;
    const auto scalar=gemm(a,b,Backend::Scalar);
    const auto tiled=gemm(a,b,Backend::Tiled);
    const auto automatic=gemm(a,b,Backend::Auto);
    for(std::size_t i=0;i<scalar.values.size();++i) {
        assert(near(scalar.values[i],tiled.values[i]));
        assert(near(scalar.values[i],automatic.values[i]));
    }
    if(simd_available()) {
        const auto simd=gemm(a,b,Backend::Simd);
        for(std::size_t i=0;i<scalar.values.size();++i)
            assert(near(scalar.values[i],simd.values[i]));
    }else assert(rejects([&]{gemm(a,b,Backend::Simd);}));
    assert(rejects([&]{gemm(b,b);}));
    a.values[0]=std::numeric_limits<double>::quiet_NaN();
    assert(rejects([&]{gemm(a,b);}));

    Matrix x(1,2);x.at(0,0)=0.8;x.at(0,1)=-0.4;
    Matrix w(2,2);w.at(0,0)=0.1;w.at(0,1)=0.25;
    w.at(1,0)=-0.2;w.at(1,1)=0.3;
    Tape tape;
    auto xid=tape.leaf(x),wid=tape.leaf(w);
    const auto scores=tape.matmul(xid,wid);
    const auto ce=tape.cross_entropy(scores,1);
    tape.backward(ce);
    const double g=tape.grad(wid).at(0,1);
    const double epsilon=1e-5;
    auto empirical=[&](double weight){
        Matrix changed=w;changed.at(0,1)=weight;
        Tape another;
        auto a1=another.leaf(x), b1=another.leaf(changed);
        return another.value(another.cross_entropy(another.matmul(a1,b1),1)).at(0,0);
    };
    const double fd=(empirical(w.at(0,1)+epsilon)-empirical(w.at(0,1)-epsilon))/(2*epsilon);
    assert(near(g,fd,1e-5));
    assert(tape.size()==4);
    assert(rejects([&]{tape.cross_entropy(scores,9);}));
    Matrix score(3,3);
    Tape causal;
    const auto sid=causal.leaf(score);
    const auto attention=causal.softmax(sid,true);
    assert(causal.value(attention).at(0,1)==0.0);
    assert(near(causal.value(attention).at(2,2),1.0/3.0));
    assert(rejects([&]{causal.gather_rows(sid,{8});}));
    // Reusing a tape leaf must accumulate both gradient contributions.
    Matrix repeatedInput(1,1,2.0);
    Tape repeatedTape;
    const auto repeatId=repeatedTape.leaf(repeatedInput);
    const auto doubled=repeatedTape.add(repeatId,repeatId);
    repeatedTape.backward(doubled);
    assert(near(repeatedTape.grad(repeatId).at(0,0),2.0));
    // Failed optimizer steps must preserve the original weights bit-for-bit.
    Matrix guarded(1,2);
    guarded.values={1.0,1e8};
    Matrix riskyGradient(1,2);
    riskyGradient.values={1.0,-1e8};
    const auto before=guarded.values;
    assert(rejects([&]{sgd_update(guarded,riskyGradient,0.2,100.0);}));
    assert(guarded.values==before);


    const std::string corpus=std::string("aba aba aba\n")+
                            "hello world\nhello world\nhello world\n"+
                            "hello world\nhello world\nhello world\n";
    auto tokenizer=SubwordTokenizer::train(corpus,12);
    assert(!tokenizer.merges.empty() && tokenizer.vocab_size()<=108);
    assert(tokenizer.decode(tokenizer.encode(corpus))==corpus);
    assert(tokenizer.merges==SubwordTokenizer::train(corpus,12).merges);
    assert(rejects([&]{tokenizer.encode("bad\tinput");}));
    assert(rejects([&]{tokenizer.decode({999});}));
    TransformerOptions opt;opt.steps=120;opt.context=3;opt.hidden=6;
    opt.merges=3;opt.seed=7;opt.learning_rate=0.03;
    double loss=0;
    const auto model=train_transformer(corpus,opt,&loss);
    assert(std::isfinite(loss) && loss>0 && loss<10);
    assert(model.output.values.size()==model.hidden*model.tokenizer.vocab_size());
    const auto repeated=train_transformer(corpus,opt);
    assert(repeated.output.values==model.output.values);
    const auto model_path=root/"test-transformer.nqtf";
    save_transformer(model,model_path.string());
    const auto restored=load_transformer(model_path.string());
    const auto left=generate_transformer(model,"hello",16,0.0);
    const auto right=generate_transformer(restored,"hello",16,0.0);
    assert(left==right && !left.empty());
    assert(generate_transformer(restored,"hello",10,0.7,42)==
           generate_transformer(restored,"hello",10,0.7,42));
    assert(rejects([&]{generate_transformer(restored,"hello",65);}));
    assert(rejects([&]{generate_transformer(restored,"hello\t",2);}));
    assert(rejects([&]{generate_transformer(restored,"hello",2,-1);}));
    assert(rejects([&]{train_transformer("tiny",opt);}));
    opt.steps=0;assert(rejects([&]{train_transformer(corpus,opt);}));
    {
        std::ofstream out(root/"corrupt.nqtf",std::ios::binary);
        out<<"NQTF1 99 999 999\n";
    }
    assert(rejects([&]{load_transformer((root/"corrupt.nqtf").string());}));
    std::cout<<"PASS: scalar/tiled/SIMD tensors, finite-difference autograd, "
                "BPE, trainable causal transformer, deterministic model IO\n";
    return 0;
}
