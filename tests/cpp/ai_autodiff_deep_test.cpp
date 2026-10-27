// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/ai_framework.hpp"
#include "noqeri/ai_framework_c.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
using namespace noqeri_ai;
using namespace noqeri_ai::framework;
bool near(double a,double b,double tolerance=1e-5) {
    return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));
}
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
double loss_value(const Matrix& x,const Matrix& w) {
    Tape t;
    auto a=t.leaf(x),b=t.leaf(w);
    const auto h=t.layer_norm(t.gelu(a),1e-5);
    return t.value(t.cross_entropy(t.matmul(h,b),1)).at(0,0);
}
int main() {
    Matrix x(1,3);x.values={-0.5,0.4,1.2};
    Matrix w(3,2);w.values={0.4,-0.2,0.1,0.3,-0.1,0.5};
    Tape tape;
    auto a=tape.leaf(x),b=tape.leaf(w);
    const auto gelu=tape.gelu(a);
    const auto norm=tape.layer_norm(gelu);
    const auto logits=tape.matmul(norm,b);
    const auto loss=tape.cross_entropy(logits,1);
    tape.backward(loss);
    const auto analytic=tape.grad(a);
    const double eps=1e-5;
    for(std::size_t i=0;i<x.values.size();++i) {
        auto plus=x,minus=x;
        plus.values[i]+=eps;minus.values[i]-=eps;
        const double fd=(loss_value(plus,w)-loss_value(minus,w))/(2*eps);
        assert(near(fd,analytic.values[i],1e-4));
    }
    assert(rejects([&]{tape.layer_norm(a,0.0);}));
    assert(rejects([&]{tape.layer_norm(a,-1.0);}));
    assert(rejects([&]{tape.layer_norm(a,std::numeric_limits<double>::quiet_NaN());}));

    for(double value : {-1.0,0.0,2.0}) {
        Tape t;
        const auto leaf=t.leaf(Matrix(1,1,value));
        const auto relu=t.relu(leaf);
        t.backward(relu);
        assert(near(t.value(relu).at(0,0),std::max(0.0,value)));
        assert(near(t.grad(leaf).at(0,0),value>0?1.0:0.0));
    }
    {
        // Layer norm of identical values is zero, with zero gradient
        // for an upstream all-ones scalar reduction.
        Tape t;
        const auto leaf=t.leaf(Matrix(1,2,7.0));
        const auto normalized=t.layer_norm(leaf,1e-5);
        t.backward(t.matmul(normalized,t.leaf(Matrix(2,1,1.0))));
        assert(std::abs(t.grad(leaf).at(0,0))<1e-8);
        assert(std::abs(t.grad(leaf).at(0,1))<1e-8);
    }

    // Exercise the exact native C ABI graph, including 10=GELU, 11=LayerNorm.
    nq_ai_tape_step steps[6]{};
    steps[0].operation=0;steps[0].rows=1;steps[0].cols=3;
    steps[0].data=x.values.data();
    steps[1].operation=10;steps[1].a=0;
    steps[2].operation=11;steps[2].a=1;steps[2].scalar=1e-5;
    steps[3].operation=0;steps[3].rows=3;steps[3].cols=2;
    steps[3].data=w.values.data();
    steps[4].operation=2;steps[4].a=2;steps[4].b=3;
    steps[5].operation=7;steps[5].a=4;steps[5].target=1;
    double out=-99,grad[3]={-99,-99,-99};
    std::size_t rows=99,cols=99;
    nq_ai_tape_request request{steps,6,5,5,0,1,0,&out,1,
                               grad,3,&rows,&cols};
    assert(nq_ai_tape_execute(&request)==0);
    assert(rows==1 && cols==1);
    assert(near(out,tape.value(loss).at(0,0)));
    for(std::size_t i=0;i<3;++i)
        assert(near(grad[i],analytic.values[i],1e-5));
    request.gradient_capacity=2;out=-99;rows=99;cols=99;grad[0]=-99;
    assert(nq_ai_tape_execute(&request)==-3);
    assert(out==-99 && grad[0]==-99 && rows==99 && cols==99);
    request.gradient_capacity=3;steps[2].scalar=0.0;
    assert(nq_ai_tape_execute(&request)==-5);
    assert(out==-99 && grad[0]==-99 && rows==99 && cols==99);
}
