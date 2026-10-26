// SPDX-License-Identifier: GPL-3.0-only
#include "noqeri/lm.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
namespace fs=std::filesystem;
using namespace noqeri_ai;
template<class F> bool rejects(F fn) {
    try { fn(); return false; } catch (const std::exception&) { return true; }
}
int main(int argc,char** argv) {
    assert(argc==2);
    const fs::path root=argv[1];
    fs::create_directories(root);
    Matrix a(2,2); a.at(0,0)=1; a.at(0,1)=2; a.at(1,0)=3; a.at(1,1)=4;
    Matrix b(2,1); b.at(0,0)=2; b.at(1,0)=3;
    auto c=matmul(a,b);
    assert(c.rows==2 && c.cols==1 && c.at(0,0)==8 && c.at(1,0)==18);
    auto p=softmax_rows(a);
    assert(std::abs(p.at(0,0)+p.at(0,1)-1.0)<1e-12);
    auto normal=rms_norm_rows(a);
    assert(std::isfinite(normal.at(1,1)));
    Matrix q(3,2); Matrix k(3,2); Matrix v(3,1);
    v.at(0,0)=10;v.at(1,0)=20;v.at(2,0)=30;
    auto causal=causal_attention(q,k,v);
    assert(causal.at(0,0)==10);
    assert(std::abs(causal.at(1,0)-15.0)<1e-12);
    assert(std::abs(causal.at(2,0)-20.0)<1e-12);
    assert(rejects([&]{matmul(a,v);}));
    assert(rejects([&]{Matrix too_big(2048,2048);}));
    a.values[0]=std::numeric_limits<double>::quiet_NaN();
    assert(rejects([&]{softmax_rows(a);}));

    const std::string corpus="hello world\nhello world\nhello world\nhello world\n";
    const auto data_file=root/"corpus.txt";
    { std::ofstream out(data_file,std::ios::binary); out<<corpus; }
    assert(read_language_corpus(data_file.string())==corpus);
    const auto windows_file=root/"windows-crlf.txt";
    {
        std::ofstream out(windows_file,std::ios::binary);
        for (char c : corpus) {
            if(c=='\n')out<<"\r\n";
            else out.put(c);
        }
    }
    assert(read_language_corpus(windows_file.string())==corpus);
    {
        std::ofstream out(root/"bare-cr.txt",std::ios::binary);
        out<<"hello\rworld\rnot-valid";
    }
    assert(rejects([&]{read_language_corpus((root/"bare-cr.txt").string());}));
    LanguageOptions opt; opt.steps=2200;opt.seed=7;opt.hidden=12;opt.context=4;
    double trailing_loss=0;
    auto model=train_language(corpus,opt,&trailing_loss);
    assert(std::isfinite(trailing_loss) && trailing_loss>0 && trailing_loss<4.5);
    auto repeat=train_language(corpus,opt);
    assert(repeat.projection==model.projection && repeat.embedding==model.embedding);
    const auto file=root/"example.nqlm";
    save_language(model,file.string());
    auto loaded=load_language(file.string());
    const auto first=generate_language(model,"hello",32,0.0);
    const auto second=generate_language(loaded,"hello",32,0.0);
    assert(first==second && first.size()==32);
    assert(generate_language(loaded,"world",12,0.7,25) ==
           generate_language(loaded,"world",12,0.7,25));
    assert(rejects([&]{generate_language(loaded,"hello",257);}));
    assert(rejects([&]{generate_language(loaded,"héllo",2);}));
    assert(rejects([&]{generate_language(loaded,"hi\t",2);}));
    assert(rejects([&]{generate_language(loaded,"hello",2,-1);}));
    assert(rejects([&]{train_language("tiny",opt);}));
    { std::ofstream out(root/"bad.nqlm");out<<"NQLM1 64 65\n"; }
    assert(rejects([&]{load_language((root/"bad.nqlm").string());}));
    std::cout<<"PASS: bounded CPU matmul, stable softmax, causal attention and trainable next-token model\n";
}
