// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework_c.h"
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <exception>
#include <cmath>

extern "C" int64_t nq_ai_gemm(const nq_ai_gemm_request* request) {
    if (!request || !request->left || !request->right || !request->output) return -1;
    auto shape = [](std::size_t rows,std::size_t cols) {
        return rows > 0 && cols > 0 && rows <= 2048 && cols <= 2048
            && rows <= 1048576 / cols;
    };
    if (!shape(request->left_rows,request->left_cols) ||
        !shape(request->right_rows,request->right_cols) ||
        !shape(request->left_rows,request->right_cols) ||
        request->left_cols != request->right_rows) return -2;
    const std::size_t outputLength=request->left_rows*request->right_cols;
    if (request->output_capacity < outputLength) return -3;
    if (request->backend > 3u) return -4;
    try {
        using namespace noqeri_ai::framework;
        noqeri_ai::Matrix a(request->left_rows,request->left_cols);
        noqeri_ai::Matrix b(request->right_rows,request->right_cols);
        std::copy_n(request->left,a.values.size(),a.values.begin());
        std::copy_n(request->right,b.values.size(),b.values.begin());
        Backend backend=Backend::Auto;
        switch(request->backend){
            case 0:backend=Backend::Scalar;break;
            case 1:backend=Backend::Tiled;break;
            case 2:backend=Backend::Simd;break;
            default:break;
        }
        noqeri_ai::Matrix result=gemm(a,b,backend);
        // Write once, after all shape, value and numerical checks succeed.
        std::copy(result.values.begin(),result.values.end(),request->output);
        return 0;
    }catch(const std::exception&){return -5;}
     catch(...){return -5;}
}

extern "C" int64_t nq_ai_softmax(const nq_ai_softmax_request* r) {
    if (!r || !r->input || !r->output) return -1;
    if (!r->rows || !r->cols || r->rows > 2048 ||
        r->cols > 2048 || r->rows > 1048576 / r->cols) return -2;
    if (r->output_capacity < r->rows*r->cols) return -3;
    if (r->causal > 1 || (r->causal && (r->rows != r->cols || r->rows > 256))) return -4;
    try {
        noqeri_ai::Matrix x(r->rows,r->cols);
        std::copy_n(r->input,x.values.size(),x.values.begin());
        noqeri_ai::framework::Tape tape;
        const auto result=tape.softmax(tape.leaf(x),r->causal==1);
        const auto& values=tape.value(result).values;
        std::copy(values.begin(),values.end(),r->output);
        return 0;
    } catch (const std::exception&) { return -5; } catch (...) { return -5; }
}
extern "C" int64_t nq_ai_sgd(const nq_ai_sgd_request* r) {
    if (!r || !r->parameters || !r->gradients || !r->output) return -1;
    if (!r->count || r->count > 1048576) return -2;
    if (r->output_capacity < r->count) return -3;
    if (!std::isfinite(r->learning_rate) || !std::isfinite(r->gradient_clip) ||
        r->learning_rate <= 0 || r->learning_rate > 0.2 ||
        r->gradient_clip <= 0 || r->gradient_clip > 100) return -4;
    try {
        noqeri_ai::Matrix params(1,r->count),grads(1,r->count);
        std::copy_n(r->parameters,r->count,params.values.begin());
        std::copy_n(r->gradients,r->count,grads.values.begin());
        noqeri_ai::framework::sgd_update(params,grads,r->learning_rate,r->gradient_clip);
        std::copy(params.values.begin(),params.values.end(),r->output);
        return 0;
    } catch (const std::exception&) { return -5; } catch (...) { return -5; }
}
