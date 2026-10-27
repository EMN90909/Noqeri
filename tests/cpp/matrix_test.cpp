// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/matrix.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace noqeri_ai;
namespace m=noqeri_ai::matrix;
template<class F> bool throws(F fn) {try{fn();return false;}catch(const std::exception&){return true;}}
bool near(double a,double b) {return std::abs(a-b)<1e-9;}
int main() {
    Matrix a(2,3);a.values={1,2,3,4,5,6};
    assert(m::identity(3).at(2,2)==1);
    assert(m::transpose(a).at(2,1)==6);
    assert(m::reshape(a,3,2).at(1,0)==3);
    assert(m::slice(a,0,2,1,3).values==std::vector<double>({2,3,5,6}));
    Matrix row(1,3);row.values={10,20,30};
    assert(m::add(a,row).values==std::vector<double>({11,22,33,14,25,36}));
    Matrix col(2,1);col.values={2,3};
    assert(m::multiply(a,col).at(1,2)==18);
    assert(m::subtract(a,row).at(0,0)==-9);
    assert(near(m::divide(a,col).at(1,0),4.0/3));
    assert(m::concatenate_rows(a,a).rows==4);
    assert(m::concatenate_cols(a,a).cols==6);
    assert(near(m::sum(a,0).at(0,1),7));
    assert(near(m::mean(a,1).at(1,0),5));
    assert(near(m::variance(a,0).at(0,0),2.25));
    assert(near(m::normalize_columns(a).at(0,1),-1));
    assert(near(m::dot(a,a),91));
    assert(near(m::l2_norm(m::ones(1,4)),2));
    assert(near(m::sigmoid(Matrix(1,1,100)).at(0,0),1));
    assert(near(m::sigmoid(Matrix(1,1,-100)).at(0,0),0));
    assert(m::apply_relu(m::scale(a,-1)).at(0,0)==0);
    assert(m::clip(a,2,4).at(1,2)==4);
    Matrix k(2,2,1.0);
    assert(m::conv2d_valid(a,k).values==std::vector<double>({12,16}));
    assert(m::conv2d_valid(a,k,2).values==std::vector<double>({12}));
    assert(throws([&]{m::divide(a,Matrix(1,1,0));}));
    assert(throws([&]{m::reshape(a,4,2);}));
    assert(throws([&]{m::slice(a,0,3,0,2);}));
    assert(throws([&]{m::conv2d_valid(a,k,0);}));
    // Guard potentially enormous convolution workloads before iterating.
    assert(throws([&]{m::conv2d_valid(Matrix(1000,1000),Matrix(30,30));}));
    assert(throws([&]{m::sum(a,2);}));
    assert(throws([&]{m::add(a,Matrix(4,3));}));
    assert(throws([&]{m::scale(a,std::numeric_limits<double>::infinity());}));
    assert(throws([&]{m::normalize_columns(a,0);}));
    assert(throws([&]{m::identity(2049);}));
    assert(throws([&]{m::scale(Matrix(1,1,1e8),100);}));
}
