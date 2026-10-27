// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_matrix_c.h"
#include "noqeri/matrix.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>

namespace {
bool shape(std::size_t rows,std::size_t cols) {
    return rows && cols && rows<=2048 && cols<=2048 &&
           rows<=1048576/cols;
}
bool binary(std::uint32_t op) {
    return op==0 || op==8 || (op>=14 && op<=17) || op==22;
}
bool uses_axis(std::uint32_t op) {
    return (op>=2 && op<=4) || (op>=12 && op<=13) ||
           (op>=18 && op<=20);
}
} // namespace

extern "C" int64_t nq_ai_matrix_execute(const nq_ai_matrix_request* r) {
    if(!r || !r->left || !r->output || !r->output_rows || !r->output_cols)
        return -1;
    if(r->operation>24) return -4;
    if(!shape(r->left_rows,r->left_cols)) return -2;
    const bool has_rhs=binary(r->operation);
    if(has_rhs && !r->right) return -1;
    if(has_rhs && !shape(r->right_rows,r->right_cols)) return -2;
    if(uses_axis(r->operation) && r->axis>1) return -4;
    if(r->operation==22 && r->axis==0) return -4;
    try {
        using noqeri_ai::Matrix;
        namespace m=noqeri_ai::matrix;
        Matrix a(r->left_rows,r->left_cols);
        std::copy_n(r->left,a.values.size(),a.values.begin());
        Matrix b;
        if(has_rhs) {
            b=Matrix(r->right_rows,r->right_cols);
            std::copy_n(r->right,b.values.size(),b.values.begin());
        }
        Matrix result;
        switch(r->operation) {
            case 0: result=m::matmul(a,b);break;
            case 1: result=m::transpose(a);break;
            case 2: result=m::softmax(a,r->axis);break;
            case 3: result=m::log_softmax(a,r->axis);break;
            case 4: result=m::logsumexp(a,r->axis);break;
            case 5: result=m::layer_norm_rows(a,r->epsilon);break;
            case 6: result=m::gelu(a);break;
            case 7: result=m::softplus(a);break;
            case 8: result=m::solve(a,b);break;
            case 9: result=m::inverse(a);break;
            case 10: result=m::cholesky(a);break;
            case 11: result=m::covariance_columns(a);break;
            case 12: result=m::maximum(a,r->axis);break;
            case 13: result=m::minimum(a,r->axis);break;
            case 14: result=m::add(a,b);break;
            case 15: result=m::subtract(a,b);break;
            case 16: result=m::multiply(a,b);break;
            case 17: result=m::divide(a,b);break;
            case 18: result=m::sum(a,r->axis);break;
            case 19: result=m::mean(a,r->axis);break;
            case 20: result=m::variance(a,r->axis);break;
            case 21: result=m::normalize_columns(a,r->epsilon);break;
            case 22: result=m::conv2d_valid(a,b,r->axis);break;
            case 23: result=Matrix(1,1,m::trace(a));break;
            case 24: result=Matrix(1,1,m::l1_norm(a));break;
            default: return -4;
        }
        if(r->output_capacity<result.values.size())return -3;
        // Publish only after the operation and capacity validation succeed.
        std::copy(result.values.begin(),result.values.end(),r->output);
        *r->output_rows=result.rows;
        *r->output_cols=result.cols;
        return 0;
    }catch(const std::exception&) {return -5;}
     catch(...) {return -5;}
}
