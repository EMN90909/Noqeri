// SPDX-License-Identifier: Apache-2.0
// Bounded f64 AdamW optimizer and robust global gradient norm.
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace noqeri_ai::framework {
namespace {
void demand(bool condition, const char* why) {
    if (!condition) throw std::invalid_argument(std::string("Noqeri optimizer: ")+why);
}
void valid(const Matrix& x) {
    demand(x.rows && x.cols && x.rows<=2048 && x.cols<=2048 &&
           x.rows<=1048576/x.cols && x.values.size()==x.rows*x.cols,
           "invalid matrix shape");
    for(double v : x.values) demand(std::isfinite(v) && std::abs(v)<=1e8,
                                   "nonfinite or oversized matrix value");
}
void options_valid(const AdamWOptions& o) {
    demand(std::isfinite(o.learning_rate) && o.learning_rate>0 && o.learning_rate<=0.2,
           "invalid learning rate");
    demand(std::isfinite(o.beta1) && o.beta1>=0 && o.beta1<1 &&
           std::isfinite(o.beta2) && o.beta2>=0 && o.beta2<1,"invalid betas");
    demand(std::isfinite(o.epsilon) && o.epsilon>=1e-12 && o.epsilon<=1e-2,
           "invalid epsilon");
    demand(std::isfinite(o.weight_decay) && o.weight_decay>=0 && o.weight_decay<=1,
           "invalid decoupled weight decay");
}
} // namespace

double global_gradient_norm(const std::vector<const Matrix*>& gradients) {
    demand(!gradients.empty() && gradients.size()<=128,"invalid gradient collection");
    // Elements are already bounded by 1e8 and the entire collection by
    // 128 * 1,048,576, so a long-double sum of squares cannot overflow.
    // Avoid the much more expensive per-element hypot for dense training.
    long double squared=0;
    for(const Matrix* gradient : gradients) {
        demand(gradient!=nullptr,"null gradient pointer");
        valid(*gradient);
        for(double v:gradient->values)
            squared+=static_cast<long double>(v)*v;
    }
    const double norm=std::sqrt(squared);
    demand(std::isfinite(norm),"nonfinite gradient norm");
    return norm;
}

double clip_grad_norm(std::vector<Matrix*>& gradients,double max_norm) {
    demand(std::isfinite(max_norm) && max_norm>0 && max_norm<=1e8,
           "invalid maximum gradient norm");
    demand(!gradients.empty() && gradients.size()<=128,"invalid gradient collection");
    std::vector<const Matrix*> input;
    input.reserve(gradients.size());
    for(std::size_t i=0;i<gradients.size();++i) {
        demand(gradients[i]!=nullptr,"null gradient pointer");
        for(std::size_t j=0;j<i;++j)
            demand(gradients[i]!=gradients[j],"aliased gradient destinations");
        input.push_back(gradients[i]);
    }
    const double norm=global_gradient_norm(input);
    if(norm<=max_norm)return norm;
    const double multiplier=max_norm/norm;
    // Validate all transformed values before changing any tensor.
    std::vector<Matrix> staged;
    staged.reserve(gradients.size());
    for(Matrix* g:gradients) {
        staged.push_back(*g);
        for(double& v:staged.back().values)v*=multiplier;
        valid(staged.back());
    }
    for(std::size_t i=0;i<gradients.size();++i)
        gradients[i]->values.swap(staged[i].values);
    return norm;
}

AdamWOptimizer::AdamWOptimizer(const std::vector<Matrix*>& parameters) {
    demand(!parameters.empty() && parameters.size()<=128,"invalid parameter collection");
    for(std::size_t i=0;i<parameters.size();++i) {
        const Matrix* param=parameters[i];
        demand(param!=nullptr,"null parameter");
        for(std::size_t j=0;j<i;++j)
            demand(param!=parameters[j],"duplicate parameter pointer");
        valid(*param);
        first_.emplace_back(param->rows,param->cols);
        second_.emplace_back(param->rows,param->cols);
    }
}
std::size_t AdamWOptimizer::steps() const noexcept {return steps_;}
AdamWState AdamWOptimizer::state() const {
    return AdamWState{first_,second_,steps_};
}
void AdamWOptimizer::restore(const AdamWState& state) {
    demand(state.steps<=1000000 && state.first.size()==first_.size() &&
           state.second.size()==second_.size(),"invalid checkpoint optimizer state");
    for(std::size_t i=0;i<first_.size();++i) {
        valid(state.first[i]);valid(state.second[i]);
        demand(state.first[i].rows==first_[i].rows &&
               state.first[i].cols==first_[i].cols &&
               state.second[i].rows==second_[i].rows &&
               state.second[i].cols==second_[i].cols,"checkpoint optimizer shapes differ");
        for(double v:state.second[i].values)
            demand(v>=0,"negative optimizer second moment");
    }
    auto first=state.first,second=state.second;
    first_.swap(first);second_.swap(second);steps_=state.steps;
}


void AdamWOptimizer::step(const std::vector<Matrix*>& parameters,
                          const std::vector<const Matrix*>& gradients,
                          const AdamWOptions& options) {
    options_valid(options);
    demand(parameters.size()==first_.size() && gradients.size()==first_.size(),
           "optimizer state shape mismatch");
    demand(steps_<1000000,"optimizer step budget exhausted");
    // expm1(log(beta) * t) avoids cancellation for beta close to one.
    const auto correction=[](double beta,std::size_t t) {
        return beta==0 ? 1.0 :
            -std::expm1(static_cast<double>(t)*std::log(beta));
    };
    const double correction1=correction(options.beta1,steps_+1);
    const double correction2=correction(options.beta2,steps_+1);
    demand(correction1>0 && correction2>0,"invalid bias correction");

    // Stage parameters AND optimizer moments. Failure must not advance
    // steps or alter any tensor, including the unaffected parameter groups.
    std::vector<Matrix> next_weights,next_first,next_second;
    next_weights.reserve(parameters.size());
    next_first.reserve(parameters.size());
    next_second.reserve(parameters.size());
    for(std::size_t k=0;k<parameters.size();++k) {
        demand(parameters[k] && gradients[k],"null optimizer input");
        for(std::size_t j=0;j<k;++j)
            demand(parameters[k]!=parameters[j],"duplicate parameter pointer");
        const Matrix& w=*parameters[k];
        const Matrix& g=*gradients[k];
        valid(w);valid(g);
        demand(w.rows==first_[k].rows && w.cols==first_[k].cols &&
               w.rows==g.rows && w.cols==g.cols,"optimizer tensor shape mismatch");
        next_weights.push_back(w);
        next_first.push_back(first_[k]);
        next_second.push_back(second_[k]);
        for(std::size_t j=0;j<w.values.size();++j) {
            const double grad=g.values[j];
            const double m=options.beta1*first_[k].values[j]+(1-options.beta1)*grad;
            const double v=options.beta2*second_[k].values[j]+
                           (1-options.beta2)*grad*grad;
            const double estimate=m/correction1;
            const double deviation=v/correction2;
            demand(std::isfinite(m) && std::isfinite(v) && v>=0 &&
                   std::isfinite(estimate) && std::isfinite(deviation),
                   "nonfinite optimizer moment");
            const double next=w.values[j]*(1-options.learning_rate*options.weight_decay)-
                options.learning_rate*estimate/(std::sqrt(deviation)+options.epsilon);
            demand(std::isfinite(next) && std::abs(next)<=1e8,
                   "parameter update out of bounds");
            next_weights[k].values[j]=next;
            next_first[k].values[j]=m;
            next_second[k].values[j]=v;
        }
    }
    for(std::size_t k=0;k<parameters.size();++k) {
        parameters[k]->values.swap(next_weights[k].values);
        first_[k].values.swap(next_first[k].values);
        second_[k].values.swap(next_second[k].values);
    }
    ++steps_;
}

double cosine_learning_rate(std::size_t step,std::size_t total_steps,
                            double peak,double min_rate,std::size_t warmup) {
    demand(total_steps>0 && step<total_steps && warmup<total_steps &&
           std::isfinite(peak) && peak>0 && peak<=0.2 &&
           std::isfinite(min_rate) && min_rate>=0 && min_rate<=peak,
           "invalid learning-rate schedule");
    if(step<warmup)
        return peak*static_cast<double>(step+1)/static_cast<double>(warmup);
    const std::size_t decay_steps=total_steps-warmup;
    if(decay_steps<=1)return peak;
    const double progress=static_cast<double>(step-warmup)/
                          static_cast<double>(decay_steps-1);
    constexpr double pi=3.1415926535897932384626433832795;
    return min_rate+(peak-min_rate)*0.5*(1+std::cos(pi*progress));
}
} // namespace noqeri_ai::framework
