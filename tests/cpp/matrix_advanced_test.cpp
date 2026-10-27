// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/matrix.hpp"
#include "noqeri/ai_matrix_c.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

using noqeri_ai::Matrix;
namespace m=noqeri_ai::matrix;
bool near(double a,double b,double tolerance=1e-9) {
    return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));
}
template<typename F> bool rejects(F f) {
    try {f();return false;}catch(const std::exception&) {return true;}
}
int main() {
    Matrix a(2,2);a.values={4,7,2,6};
    Matrix rhs(2,2);rhs.values={1,0,0,1};
    auto product=m::matmul(a,rhs);
    assert(product.values==a.values);
    assert(near(m::trace(a),10) && m::diagonal(a).values==std::vector<double>({4,6}));
    const auto inv=m::inverse(a);
    assert(near(inv.at(0,0),0.6) && near(inv.at(0,1),-0.7));
    assert(near(inv.at(1,0),-0.2) && near(inv.at(1,1),0.4));
    const auto solved=m::solve(a,rhs);
    for(std::size_t j=0;j<4;++j)assert(near(solved.values[j],inv.values[j]));
    const auto round=m::matmul(a,inv);
    for(std::size_t i=0;i<2;++i)
        for(std::size_t j=0;j<2;++j)
            assert(near(round.at(i,j),i==j?1.0:0.0));
    Matrix singular(2,2);singular.values={1,2,2,4};
    assert(rejects([&]{m::solve(singular,rhs);}));
    assert(rejects([&]{m::inverse(singular);}));
    assert(rejects([&]{m::solve(a,Matrix(3,1));}));
    assert(rejects([&]{m::inverse(Matrix(129,129));}));
    Matrix spd(2,2);spd.values={4,2,2,3};
    const auto l=m::cholesky(spd);
    assert(near(l.at(0,0),2)&&near(l.at(1,0),1));
    assert(near(l.at(1,1),std::sqrt(2.0))&&l.at(0,1)==0);
    Matrix nonspd(2,2);nonspd.values={1,2,2,1};
    assert(rejects([&]{m::cholesky(nonspd);}));
    nonspd.values={1,2,3,4};
    assert(rejects([&]{m::cholesky(nonspd);}));

    Matrix logits(2,3);logits.values={1000,1000,-1000,-1,0,1};
    const auto probabilities=m::softmax(logits);
    const auto logs=m::log_softmax(logits);
    const auto lse=m::logsumexp(logits);
    assert(near(probabilities.at(0,0),0.5)&&near(probabilities.at(0,1),0.5));
    assert(probabilities.at(0,2)==0.0);
    assert(near(logs.at(0,0),-std::log(2.0)));
    assert(near(lse.at(0,0),1000+std::log(2.0)));
    assert(near(probabilities.at(1,0)+probabilities.at(1,1)+probabilities.at(1,2),1.0));
    assert(near(m::softmax(logits,0).at(0,0)+m::softmax(logits,0).at(1,0),1.0));
    assert(m::maximum(logits).values==std::vector<double>({1000,1}));
    assert(m::minimum(logits).values==std::vector<double>({-1000,-1}));
    assert(m::argmax(logits)==std::vector<std::size_t>({0,2}));
    assert(m::argmax(logits,0)==std::vector<std::size_t>({0,0,1}));
    Matrix equal(2,3,12);
    assert(m::argmax(equal)==std::vector<std::size_t>({0,0}));
    assert(rejects([&]{m::softmax(logits,2);}));
    assert(rejects([&]{m::logsumexp(logits,99);}));
    assert(near(m::layer_norm_rows(equal).at(1,2),0));
    Matrix centered(1,3);centered.values={1,2,3};
    auto normed=m::layer_norm_rows(centered);
    assert(near(m::mean(normed,1).at(0,0),0,1e-7));
    assert(near(m::variance(normed,1).at(0,0),1,1e-4));
    assert(rejects([&]{m::layer_norm_rows(centered,0);}));
    assert(rejects([&]{m::layer_norm_rows(centered,std::numeric_limits<double>::quiet_NaN());}));
    assert(near(m::gelu(Matrix(1,1,0)).at(0,0),0));
    assert(near(m::softplus(Matrix(1,1,1000)).at(0,0),1000));
    assert(near(m::softplus(Matrix(1,1,-1000)).at(0,0),0));
    assert(near(m::l1_norm(centered),6.0));
    Matrix samples(3,2);samples.values={1,2,2,4,3,6};
    const auto cov=m::covariance_columns(samples);
    assert(near(cov.at(0,0),1)&&near(cov.at(0,1),2));
    assert(near(cov.at(1,0),2)&&near(cov.at(1,1),4));
    assert(rejects([&]{m::covariance_columns(Matrix(1,2));}));
    assert(rejects([&]{m::covariance_columns(Matrix(2,129));}));
    assert(m::gather_rows(samples,{2,0,2}).values==
           std::vector<double>({3,6,1,2,3,6}));
    assert(m::gather_columns(samples,{1,0,1}).values==
           std::vector<double>({2,1,2,4,2,4,6,3,6}));
    assert(rejects([&]{m::gather_rows(samples,{3});}));
    assert(rejects([&]{m::gather_columns(samples,{});}));
    samples.values[0]=std::numeric_limits<double>::infinity();
    assert(rejects([&]{m::softplus(samples);}));
    assert(rejects([&]{m::covariance_columns(samples);}));

    // Noqeri/C requests must preserve ALL outputs on failures.
    double input[4]={1,2,3,4}, right[4]={5,6,7,8};
    double output[4]={-99,-99,-99,-99};
    std::size_t out_rows=77,out_cols=88;
    nq_ai_matrix_request request{input,2,2,right,2,2,0,1,1e-5,
                                 output,4,&out_rows,&out_cols};
    assert(nq_ai_matrix_execute(&request)==0);
    assert(out_rows==2 && out_cols==2);
    assert(output[0]==19 && output[1]==22 && output[2]==43 && output[3]==50);
    request.operation=2;request.axis=1;request.output_capacity=3;
    output[0]=-99;out_rows=77;out_cols=88;
    assert(nq_ai_matrix_execute(&request)==-3);
    assert(output[0]==-99 && out_rows==77 && out_cols==88);
    request.output_capacity=4;request.axis=2;
    assert(nq_ai_matrix_execute(&request)==-4 && output[0]==-99);
    request.axis=1;input[0]=std::numeric_limits<double>::quiet_NaN();
    assert(nq_ai_matrix_execute(&request)==-5);
    assert(output[0]==-99 && out_rows==77 && out_cols==88);
    input[0]=1;request.operation=8;request.right=nullptr;
    assert(nq_ai_matrix_execute(&request)==-1 && output[0]==-99);
    request.right=right;request.left_rows=0;
    assert(nq_ai_matrix_execute(&request)==-2 && output[0]==-99);
    request.left_rows=2;request.operation=255;
    assert(nq_ai_matrix_execute(&request)==-4 && output[0]==-99);
    request.operation=23;
    assert(nq_ai_matrix_execute(&request)==0 && output[0]==5);
    assert(out_rows==1 && out_cols==1);
    // Aliasing is safe because input is copied before the output is committed.
    request.operation=1;request.output=input;request.output_capacity=4;
    assert(nq_ai_matrix_execute(&request)==0);
    assert(input[0]==1 && input[1]==3 && input[2]==2 && input[3]==4);
    assert(nq_ai_matrix_execute(nullptr)==-1);
}
