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
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

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
            if (s.operation > 11) return -4;
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
                case 9: id=tape.relu(s.a); break;
                case 10: id=tape.gelu(s.a); break;
                case 11: id=tape.layer_norm(s.a,s.scalar); break;
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

extern "C" int64_t nq_ai_bpe_encode(const nq_ai_bpe_request* r) {
    if (!r || !r->corpus || !r->text || !r->tokens || !r->token_count) return -1;
    if (r->corpus_length < 16 || r->corpus_length > 65536 ||
        r->text_length > 4096 || r->max_merges > 32) return -2;
    try {
        std::string corpus(reinterpret_cast<const char*>(r->corpus),r->corpus_length);
        std::string text(reinterpret_cast<const char*>(r->text),r->text_length);
        auto encoder=noqeri_ai::framework::SubwordTokenizer::train(corpus,r->max_merges);
        auto tokens=encoder.encode(text);
        if(tokens.size()>r->token_capacity)return -3;
        std::copy(tokens.begin(),tokens.end(),r->tokens);
        *r->token_count=tokens.size();
        return 0;
    } catch(const std::exception&) { return -5; } catch(...) { return -5; }
}
extern "C" int64_t nq_ai_transformer_train_generate(
    const nq_ai_transformer_request* r) {
    if (!r || !r->corpus || !r->prompt || !r->output ||
        !r->output_length || !r->training_loss) return -1;
    if(r->corpus_length < 16 || r->corpus_length > 65536 ||
       r->prompt_length > 4096 || !r->context || r->context > 16 ||
       r->hidden < 2 || r->hidden > 24 ||
       !r->steps || r->steps > 10000 || r->merges > 32 ||
       !r->output_tokens || r->output_tokens > 64 || r->backend > 3 ||
       !std::isfinite(r->learning_rate) ||
       r->learning_rate < 0.000001 || r->learning_rate > 0.1) return -2;
    try {
        std::string corpus(reinterpret_cast<const char*>(r->corpus),r->corpus_length);
        std::string prompt(reinterpret_cast<const char*>(r->prompt),r->prompt_length);
        noqeri_ai::framework::TransformerOptions opt;
        opt.context=r->context; opt.hidden=r->hidden; opt.steps=r->steps;
        opt.merges=r->merges; opt.learning_rate=r->learning_rate; opt.seed=r->seed;
        switch(r->backend){
            case 0:opt.backend=noqeri_ai::framework::Backend::Scalar;break;
            case 1:opt.backend=noqeri_ai::framework::Backend::Tiled;break;
            case 2:opt.backend=noqeri_ai::framework::Backend::Simd;break;
            default:opt.backend=noqeri_ai::framework::Backend::Auto;break;
        }
        double loss=0;
        auto model=noqeri_ai::framework::train_transformer(corpus,opt,&loss);
        auto output=noqeri_ai::framework::generate_transformer(
            model,prompt,r->output_tokens,0.0,r->seed,opt.backend);
        if(output.size()>r->output_capacity)return -3;
        std::copy(output.begin(),output.end(),r->output);
        *r->output_length=output.size();
        *r->training_loss=loss;
        return 0;
    } catch(const std::exception&) { return -5; } catch(...) { return -5; }
}

namespace {
using NqOwnedModel = noqeri_ai::framework::TransformerModel;
std::mutex nq_model_mutex;
std::unordered_map<std::uint64_t,std::shared_ptr<const NqOwnedModel>> nq_models;
std::uint64_t nq_next_model_id=1;
constexpr std::size_t nq_max_owned_models=16;
noqeri_ai::framework::Backend nq_backend(std::uint32_t backend) {
    using noqeri_ai::framework::Backend;
    switch(backend) {
        case 0:return Backend::Scalar;
        case 1:return Backend::Tiled;
        case 2:return Backend::Simd;
        default:return Backend::Auto;
    }
}
}
extern "C" int64_t nq_ai_model_train(const nq_ai_model_train_request* r) {
    if (!r || !r->corpus || !r->handle || !r->training_loss) return -1;
    if (r->corpus_length < 16 || r->corpus_length > 65536 ||
        !r->context || r->context > 16 || r->hidden < 2 ||
        r->hidden > 24 || !r->steps || r->steps > 10000 ||
        r->merges > 32 || !std::isfinite(r->learning_rate) ||
        r->learning_rate < 0.000001 || r->learning_rate > 0.1) return -2;
    if(r->backend > 3) return -4;
    // Check the resource budget early, then recheck it under the publish lock
    // to avoid races between concurrent training calls.
    { std::lock_guard<std::mutex> lock(nq_model_mutex);
      if(nq_models.size() >= nq_max_owned_models ||
         nq_next_model_id == 0 || nq_next_model_id == UINT64_MAX) return -6; }
    try {
        noqeri_ai::framework::TransformerOptions opt;
        opt.context=r->context; opt.hidden=r->hidden; opt.steps=r->steps;
        opt.merges=r->merges; opt.learning_rate=r->learning_rate;
        opt.seed=r->seed; opt.backend=nq_backend(r->backend);
        const std::string corpus(reinterpret_cast<const char*>(r->corpus),
                                 r->corpus_length);
        double loss=0.0;
        auto model=std::make_shared<const NqOwnedModel>(
            noqeri_ai::framework::train_transformer(corpus,opt,&loss));
        if(!std::isfinite(loss)) return -5;
        std::uint64_t handle=0;
        {
            std::lock_guard<std::mutex> lock(nq_model_mutex);
            if(nq_models.size() >= nq_max_owned_models ||
               nq_next_model_id == 0 || nq_next_model_id == UINT64_MAX) return -6;
            handle=nq_next_model_id++;
            nq_models.emplace(handle,std::move(model));
        }
        *r->handle=handle;
        *r->training_loss=loss;
        return 0;
    } catch (const std::exception&) { return -5; }
      catch (...) { return -5; }
}
extern "C" int64_t nq_ai_model_generate(const nq_ai_model_generate_request* r) {
    if (!r || !r->prompt || !r->output || !r->output_length) return -1;
    if(r->prompt_length > 4096 || !r->output_tokens || r->output_tokens > 64 ||
       !std::isfinite(r->temperature) ||
       r->temperature < 0.0 || r->temperature > 10.0) return -2;
    if(r->backend > 3) return -4;
    std::shared_ptr<const NqOwnedModel> model;
    {
        std::lock_guard<std::mutex> lock(nq_model_mutex);
        auto it=nq_models.find(r->handle);
        if(it==nq_models.end())return -7;
        model=it->second;
    }
    try {
        const std::string prompt(reinterpret_cast<const char*>(r->prompt),
                                 r->prompt_length);
        const auto result=noqeri_ai::framework::generate_transformer(
            *model,prompt,r->output_tokens,r->temperature,r->seed,nq_backend(r->backend));
        if(result.size()>r->output_capacity) return -3;
        std::copy(result.begin(),result.end(),r->output);
        *r->output_length=result.size();
        return 0;
    } catch (const std::exception&) { return -5; }
      catch (...) { return -5; }
}
extern "C" int64_t nq_ai_model_release(std::uint64_t handle) {
    if(handle==0) return -7;
    std::lock_guard<std::mutex> lock(nq_model_mutex);
    return nq_models.erase(handle)==1 ? 0 : -7;
}
