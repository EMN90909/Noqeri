// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/ai_framework_c.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
int main() {
    double params[2]={1,2},grad[2]={1,-1};
    double first[2]={0,0},second[2]={0,0};
    double out[2]={-99,-99},m[2]={-99,-99},v[2]={-99,-99};
    nq_ai_adamw_request req{params,grad,first,second,2,1,
        0.1,0.9,0.999,1e-8,0.1,100,
        out,m,v,2,2};
    assert(nq_ai_adamw_step(&req)==0);
    assert(std::abs(out[0]-0.89)<1e-8);
    assert(std::abs(out[1]-2.08)<1e-8);
    assert(std::abs(m[0]-0.1)<1e-9);
    assert(std::abs(m[1]+0.1)<1e-9);
    assert(std::abs(v[0]-0.001)<1e-10);
    assert(std::abs(v[1]-0.001)<1e-10);
    assert(params[0]==1 && first[0]==0 && second[0]==0);

    // Proper moment continuation, step=2; no native server state.
    req.parameters=out;req.first_moment=m;req.second_moment=v;req.step=2;
    double twice[2],twice_m[2],twice_v[2];
    req.output_parameters=twice;req.output_first_moment=twice_m;
    req.output_second_moment=twice_v;
    assert(nq_ai_adamw_step(&req)==0);
    assert(std::abs(twice[0]-(0.89*0.99-0.1))<1e-8);
    assert(std::abs(twice[1]-(2.08*0.99+0.1))<1e-8);

    // All three output arrays are left untouched on any failure.
    req.parameters=params;req.first_moment=first;req.second_moment=second;
    req.output_parameters=out;req.output_first_moment=m;req.output_second_moment=v;
    out[0]=-99;m[0]=-99;v[0]=-99;
    req.output_capacity=1;
    assert(nq_ai_adamw_step(&req)==-3);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    req.output_capacity=2;req.moment_capacity=1;
    assert(nq_ai_adamw_step(&req)==-3);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    req.moment_capacity=2;req.step=0;
    assert(nq_ai_adamw_step(&req)==-2);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    req.step=1;req.beta1=1;
    assert(nq_ai_adamw_step(&req)==-4);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    req.beta1=0.9;
    grad[0]=std::numeric_limits<double>::infinity();
    assert(nq_ai_adamw_step(&req)==-5);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    grad[0]=1;

    // Output regions must not overlap, preventing partial clobbering.
    req.output_first_moment=out;
    assert(nq_ai_adamw_step(&req)==-2);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);
    req.output_first_moment=m;

    // Input/output aliasing is supported. Stage complete results first.
    req.output_parameters=params;req.output_first_moment=first;
    req.output_second_moment=second;
    assert(nq_ai_adamw_step(&req)==0);
    assert(std::abs(params[0]-0.89)<1e-8);
    assert(std::abs(first[0]-0.1)<1e-9);

    double p2[1]={1e8},g2[1]={-1},m2[1]={0},v2[1]={0};
    req.parameters=p2;req.gradients=g2;
    req.first_moment=m2;req.second_moment=v2;
    req.count=1;req.output_parameters=out;req.output_first_moment=m;
    req.output_second_moment=v;req.learning_rate=0.2;req.weight_decay=0;
    out[0]=-99;m[0]=-99;v[0]=-99;
    assert(nq_ai_adamw_step(&req)==-5);
    assert(out[0]==-99 && m[0]==-99 && v[0]==-99);

    // Zero betas are a valid limiting case, and should not yield NaNs.
    req.parameters=params;req.gradients=grad;req.first_moment=m2;req.second_moment=v2;
    req.beta1=0;req.beta2=0;req.learning_rate=0.001;req.weight_decay=0;
    assert(nq_ai_adamw_step(&req)==0);
    assert(std::isfinite(out[0]));
    assert(nq_ai_adamw_step(nullptr)==-1);
}
