// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace noqeri_ai;
using namespace noqeri_ai::framework;
template<class F> bool rejects(F&& f) {try {f();return false;}catch(const std::exception&){return true;}}
bool near(double a,double b,double tol=1e-9) {
    return std::abs(a-b)<=tol*(1+std::max(std::abs(a),std::abs(b)));
}
int main() {
    Matrix g1(1,1,3),g2(1,1,4);
    std::vector<const Matrix*> refs{&g1,&g2};
    assert(near(global_gradient_norm(refs),5));
    std::vector<Matrix*> refs_mut{&g1,&g2};
    assert(near(clip_grad_norm(refs_mut,2.5),5));
    assert(near(g1.values[0],1.5) && near(g2.values[0],2));
    assert(near(global_gradient_norm(refs),2.5));
    assert(rejects([&]{clip_grad_norm(refs_mut,0);}));
    const auto before_g1=g1.values;
    g2.values[0]=std::numeric_limits<double>::quiet_NaN();
    assert(rejects([&]{clip_grad_norm(refs_mut,0.5);}));
    assert(g1.values==before_g1);

    Matrix w(1,2);w.values={1,2};
    Matrix grad(1,2);grad.values={1,-1};
    std::vector<Matrix*> params{&w};
    std::vector<const Matrix*> grads{&grad};
    AdamWOptimizer adamw(params);
    AdamWOptions options;
    options.learning_rate=0.1;
    options.weight_decay=0.1;
    adamw.step(params,grads,options);
    assert(adamw.steps()==1);
    assert(near(w.values[0],0.89) && near(w.values[1],2.08));
    adamw.step(params,grads,options);
    assert(adamw.steps()==2);
    assert(near(w.values[0],0.89*0.99-0.1,1e-8));
    assert(near(w.values[1],2.08*0.99+0.1,1e-8));

    // Zero gradient still applies decoupled weight decay, unlike L2
    // regularization mistakenly folded into the running moments.
    Matrix weight(1,1,10),zeros(1,1);
    AdamWOptimizer decay({&weight});
    options.learning_rate=0.01;options.weight_decay=0.5;
    decay.step({&weight},{&zeros},options);
    assert(near(weight.at(0,0),9.95,1e-10));
    assert(decay.steps()==1);

    // Full-update transaction: later group's overflowing update must not
    // change an earlier weight OR internal AdamW moments / step counter.
    Matrix first(1,1,4),last(1,1,1e8);
    Matrix pos(1,1,1),neg(1,1,-1);
    AdamWOptions bad_opts;bad_opts.learning_rate=0.2;bad_opts.weight_decay=0;
    AdamWOptimizer guarded({&first,&last});
    assert(rejects([&]{guarded.step({&first,&last},{&pos,&neg},bad_opts);}));
    assert(first.at(0,0)==4 && last.at(0,0)==1e8 && guarded.steps()==0);
    last.values[0]=1;
    guarded.step({&first,&last},{&pos,&neg},bad_opts);
    Matrix ref_first(1,1,4),ref_last(1,1,1);
    AdamWOptimizer fresh({&ref_first,&ref_last});
    fresh.step({&ref_first,&ref_last},{&pos,&neg},bad_opts);
    assert(near(first.at(0,0),ref_first.at(0,0)));
    assert(near(last.at(0,0),ref_last.at(0,0)));
    assert(guarded.steps()==1);
    options.learning_rate=-0.1;
    const auto unchanged=w.values;
    assert(rejects([&]{adamw.step(params,grads,options);}));
    assert(adamw.steps()==2 && w.values==unchanged);
    assert(rejects([&]{adamw.step(params,{},bad_opts);}));

    assert(near(cosine_learning_rate(0,10,0.1,0.001,2),0.05));
    assert(near(cosine_learning_rate(1,10,0.1,0.001,2),0.1));
    assert(near(cosine_learning_rate(2,10,0.1,0.001,2),0.1));
    assert(near(cosine_learning_rate(9,10,0.1,0.001,2),0.001));
    assert(near(cosine_learning_rate(5,10,0.1,0.1,0),0.1));
    assert(rejects([&]{cosine_learning_rate(10,10,0.1);}));
    assert(rejects([&]{cosine_learning_rate(2,10,0.1,0.2);}));

    const std::string corpus="hello hello world\nhello hello world\nhello hello world\n"
                             "hello hello world\nhello hello world\nhello hello world\n"
                             "hello hello world\nhello hello world\nhello hello world\n";
    TransformerOptions opt;
    opt.context=3;opt.hidden=6;opt.steps=28;opt.merges=3;
    opt.learning_rate=0.003;opt.seed=23;
    opt.optimizer=TrainingOptimizer::AdamW;
    opt.batch_size=4;
    opt.warmup_steps=3;
    opt.cosine_decay=true;
    opt.minimum_learning_rate=0.0001;
    opt.validation_fraction=0.2;
    TransformerTrainingReport report;
    const auto model=train_transformer(corpus,opt,nullptr,&report);
    assert(report.optimizer_steps==28 && report.train_loss>0);
    assert(report.validation_positions>0);
    assert(std::isfinite(report.validation_loss) && report.validation_loss>0);
    assert(near(report.validation_perplexity,std::exp(report.validation_loss)));
    const auto repeat=train_transformer(corpus,opt);
    assert(model.output.values==repeat.output.values);
    assert(model.embedding.values==repeat.embedding.values);
    const auto eval=evaluate_transformer(model,"hello hello world\n");
    assert(eval.positions>0 && std::isfinite(eval.loss));
    assert(near(eval.perplexity,std::exp(eval.loss)));
    auto invalid=opt;
    invalid.validation_fraction=0.5;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
    invalid=opt;invalid.batch_size=0;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
    invalid=opt;invalid.minimum_learning_rate=1;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
    invalid=opt;invalid.warmup_steps=invalid.steps;
    assert(rejects([&]{train_transformer(corpus,invalid);}));
}
