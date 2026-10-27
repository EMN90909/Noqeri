// SPDX-License-Identifier: Apache-2.0
// Stateless bounded AdamW step, native Noqeri/C ABI.
#include "noqeri/ai_framework_c.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
bool finite_weight(double v) {
    return std::isfinite(v) && std::abs(v)<=1e8;
}
bool disjoint(const double* a,const double* b,std::size_t count) {
    const auto left=reinterpret_cast<std::uintptr_t>(a);
    const auto right=reinterpret_cast<std::uintptr_t>(b);
    const auto bytes=count*sizeof(double); // count bounded <= 1,048,576
    const auto bound=std::numeric_limits<std::uintptr_t>::max();
    return left<=bound-bytes && right<=bound-bytes &&
           (left+bytes<=right || right+bytes<=left);
}
} // namespace

extern "C" int64_t nq_ai_adamw_step(const nq_ai_adamw_request* r) {
    if(!r || !r->parameters || !r->gradients || !r->first_moment ||
       !r->second_moment || !r->output_parameters ||
       !r->output_first_moment || !r->output_second_moment) return -1;
    if(!r->count || r->count>1048576 || !r->step || r->step>1000000) return -2;
    if(r->output_capacity<r->count || r->moment_capacity<r->count) return -3;
    if(!disjoint(r->output_parameters,r->output_first_moment,r->count) ||
       !disjoint(r->output_parameters,r->output_second_moment,r->count) ||
       !disjoint(r->output_first_moment,r->output_second_moment,r->count))
        return -2;
    if(!std::isfinite(r->learning_rate) || r->learning_rate<=0 ||
       r->learning_rate>0.2 ||
       !std::isfinite(r->beta1) || r->beta1<0 || r->beta1>=1 ||
       !std::isfinite(r->beta2) || r->beta2<0 || r->beta2>=1 ||
       !std::isfinite(r->epsilon) || r->epsilon<1e-12 || r->epsilon>1e-2 ||
       !std::isfinite(r->weight_decay) || r->weight_decay<0 ||
       r->weight_decay>1 ||
       !std::isfinite(r->gradient_clip_norm) ||
       r->gradient_clip_norm<=0 || r->gradient_clip_norm>1e8)
        return -4;
    try {
        // Stage all inputs before touching outputs: in-place use is supported.
        std::vector<double> params(r->parameters,r->parameters+r->count);
        std::vector<double> gradients(r->gradients,r->gradients+r->count);
        std::vector<double> first(r->first_moment,r->first_moment+r->count);
        std::vector<double> second(r->second_moment,r->second_moment+r->count);
        long double norm=0;
        for(std::size_t i=0;i<r->count;++i) {
            if(!finite_weight(params[i]) || !finite_weight(gradients[i]) ||
               !finite_weight(first[i]) || !std::isfinite(second[i]) ||
               second[i]<0 || second[i]>1e16) return -5;
            norm=std::hypot(norm,static_cast<long double>(gradients[i]));
        }
        if(!std::isfinite(norm))return -5;
        const double scale=(norm>r->gradient_clip_norm)?
            static_cast<double>(r->gradient_clip_norm/norm):1.0;
        const double correction1=1.0-std::pow(r->beta1,static_cast<double>(r->step));
        const double correction2=1.0-std::pow(r->beta2,static_cast<double>(r->step));
        if(!std::isfinite(correction1) || !std::isfinite(correction2) ||
           correction1<=0 || correction2<=0) return -5;
        for(std::size_t i=0;i<r->count;++i) {
            const double g=gradients[i]*scale;
            first[i]=r->beta1*first[i]+(1-r->beta1)*g;
            second[i]=r->beta2*second[i]+(1-r->beta2)*g*g;
            const double mhat=first[i]/correction1,vhat=second[i]/correction2;
            const double next=params[i]*(1-r->learning_rate*r->weight_decay)-
                 r->learning_rate*mhat/(std::sqrt(vhat)+r->epsilon);
            if(!finite_weight(next) || !finite_weight(first[i]) ||
               !std::isfinite(second[i]) || second[i]<0 || second[i]>1e16 ||
               !std::isfinite(vhat) || vhat<0) return -5;
            params[i]=next;
        }
        std::copy(params.begin(),params.end(),r->output_parameters);
        std::copy(first.begin(),first.end(),r->output_first_moment);
        std::copy(second.begin(),second.end(),r->output_second_moment);
        return 0;
    } catch(const std::exception&) {return -5;}
      catch(...) {return -5;}
}
