// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework_c.h"
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <exception>

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
