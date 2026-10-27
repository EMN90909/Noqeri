// SPDX-License-Identifier: Apache-2.0
// Stateless, failure-atomic AdamW step for the Noqeri native AOT C ABI.
#include "noqeri/ai_framework_c.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {
bool finite_value(double x) {
    return std::isfinite(x) && std::abs(x)<=1e8;
}
bool safe_region(const double* p,std::size_t count,std::uintptr_t& start,
                 std::uintptr_t& end) {
    if (!p || count>1048576 || count>std::numeric_limits<std::size_t>::max()/sizeof(double))
        return false;
    const auto bytes=count*sizeof(double);
    start=reinterpret_cast<std::uintptr_t>(p);
    if(start>std::numeric_limits<std::uintptr_t>::max()-bytes)return false;
    end=start+bytes;
    return true;
}
bool overlapping(std::uintptr_t as,std::uintptr_t ae,
                 std::uintptr_t bs,std::uintptr_t be) {
    return as<be && bs<ae;
}
} // namespace
extern "C" int64_t nq_ai_adamw_step(const nq_ai_adamw_request* r) {
    if(!r || !r->parameters || !r->gradients || !r->first_moment ||
       !r->second_moment || !r->output_parameters ||
       !r->output_first_moment || !r->output_second_moment)return -1;
    if(!r->count || r->count>1048576 || !r->step || r->step>1000000)return -2;
    if(r->output_capacity<r->count || r->moment_capacity<r->count)return -3;
    if(!std::isfinite(r->learning_rate) || r->learning_rate<=0 ||
       r->learning_rate>0.2 ||
       !std::isfinite(r->beta1) || r->beta1<0 || r->beta1>=1 ||
       !std::isfinite(r->beta2) || r->beta2<0 || r->beta2>=1 ||
       !std::isfinite(r->epsilon) || r->epsilon<1e-12 || r->epsilon>1e-2 ||
       !std::isfinite(r->weight_decay) || r->weight_decay<0 || r->weight_decay>1 ||
       !std::isfinite(r->gradient_clip_norm) || r->gradient_clip_norm<=0 ||
       r->gradient_clip_norm>1e8)return -4;

    std::uintptr_t ps=0,pe=0,ms=0,me=0,vs=0,ve=0;
    if(!safe_region(r->output_parameters,r->count,ps,pe) ||
       !safe_region(r->output_first_moment,r->count,ms,me) ||
       !safe_region(r->output_second_moment,r->count,vs,ve) ||
       overlapping(ps,pe,ms,me) || overlapping(ps,pe,vs,ve) ||
       overlapping(ms,me,vs,ve))return -2;
    try {
        long double norm=0;
        for(std::size_t i=0;i<r->count;++i) {
            if(!finite_value(r->parameters[i]) || !finite_value(r->gradients[i]) ||
               !finite_value(r->first_moment[i]) || !finite_value(r->second_moment[i]) ||
               r->second_moment[i]<0)return -5;
            norm=std::hypot(norm,static_cast<long double>(r->gradients[i]));
        }
        const double scale=norm>r->gradient_clip_norm ?
            static_cast<double>(r->gradient_clip_norm/norm):1.0;
        const double c1=-std::expm1(static_cast<double>(r->step)*std::log(r->beta1));
        const double c2=-std::expm1(static_cast<double>(r->step)*std::log(r->beta2));
        // Zero beta represents zero historical contribution.
        const double bias1=r->beta1==0?1:c1;
        const double bias2=r->beta2==0?1:c2;
        if(!(bias1>0 && bias2>0) || !std::isfinite(bias1) ||
           !std::isfinite(bias2))return -5;
        std::vector<double> params(r->count),first(r->count),second(r->count);
        for(std::size_t i=0;i<r->count;++i) {
            const double grad=r->gradients[i]*scale;
            const double m=r->beta1*r->first_moment[i]+(1-r->beta1)*grad;
            const double v=r->beta2*r->second_moment[i]+(1-r->beta2)*grad*grad;
            const double estimate=m/bias1;
            const double deviation=v/bias2;
            const double next=r->parameters[i]*
                (1-r->learning_rate*r->weight_decay)-
                r->learning_rate*estimate/(std::sqrt(deviation)+r->epsilon);
            if(!finite_value(m) || !finite_value(v) || v<0 ||
               !std::isfinite(estimate) || !std::isfinite(deviation) ||
               !finite_value(next))return -5;
            params[i]=next;first[i]=m;second[i]=v;
        }
        // Inputs may alias outputs; stage before committing the three regions.
        std::memcpy(r->output_parameters,params.data(),r->count*sizeof(double));
        std::memcpy(r->output_first_moment,first.data(),r->count*sizeof(double));
        std::memcpy(r->output_second_moment,second.data(),r->count*sizeof(double));
        return 0;
    } catch(...) {return -5;}
}
