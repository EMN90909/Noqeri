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

extern "C" int64_t nq_ai_tape_execute(const nq_ai_tape_request* r) {
    if (!r || !r->steps || !r->output || !r->output_rows || !r->output_cols)
        return -1;
    if (!r->step_count || r->step_count > 64 ||
        r->output_step >= r->step_count || r->backward > 1 ||
        r->backend > 3) return -2;
    if (r->backward && (!r->gradient || r->gradient_step >= r->step_count ||
                        r->loss_step >= r->step_count)) return -4;
    try {
        using namespace noqeri_ai::framework;
        Tape tape;
        switch (r->backend) {
            case 0: tape.set_backend(Backend::Scalar); break;
            case 1: tape.set_backend(Backend::Tiled); break;
            case 2: tape.set_backend(Backend::Simd); break;
            default: tape.set_backend(Backend::Auto); break;
        }
        std::size_t input_elements=0;
        for (std::size_t i=0;i<r->step_count;++i) {
            const auto& s=r->steps[i];
            if (s.operation > 8) return -4;
            if (s.operation == 0) {
                if (!s.data) return -1;
                if (!s.rows || !s.cols || s.rows > 128 ||
                    s.cols > 128 || s.rows > 16384 / s.cols)
                    return -2;
                const auto elements=s.rows*s.cols;
                if (input_elements > 262144-elements) return -2;
                input_elements+=elements;
                noqeri_ai::Matrix x(s.rows,s.cols);
                std::copy_n(s.data,elements,x.values.begin());
                if (tape.leaf(x)!=i) return -5;
                continue;
            }
            if (s.a>=i || ((s.operation==1 || s.operation==2) && s.b>=i))
                return -4;
            std::size_t id=0;
            switch(s.operation) {
                case 1: id=tape.add(s.a,s.b); break;
                case 2: id=tape.matmul(s.a,s.b); break;
                case 3: id=tape.transpose(s.a); break;
                case 4: id=tape.scale(s.a,s.scalar); break;
                case 5: id=tape.tanh(s.a); break;
                case 6:
                    if(s.target>1) return -4;
                    id=tape.softmax(s.a,s.target==1);
                    break;
                case 7: id=tape.cross_entropy(s.a,s.target); break;
                case 8: id=tape.last_row(s.a); break;
                default:return -4;
            }
            if(id!=i) return -5;
        }
        if (r->backward) {
            if (r->steps[r->loss_step].operation!=7) return -4;
            tape.backward(r->loss_step);
        }
        const auto& output=tape.value(r->output_step);
        if(r->output_capacity < output.values.size()) return -3;
        const noqeri_ai::Matrix* gradient=nullptr;
        if (r->backward) {
            gradient=&tape.grad(r->gradient_step);
            if(r->gradient_capacity < gradient->values.size()) return -3;
        }
        // All validation and autodifferentiation finished before any write.
        std::copy(output.values.begin(),output.values.end(),r->output);
        if (gradient)
            std::copy(gradient->values.begin(),gradient->values.end(),r->gradient);
        *r->output_rows=output.rows;
        *r->output_cols=output.cols;
        return 0;
    } catch (const std::exception&) { return -5; }
      catch (...) { return -5; }
}
