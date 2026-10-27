// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework.hpp"
#include "noqeri/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define NQAI_SSE2 1
#endif

namespace noqeri_ai::framework {
namespace {
void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(std::string("Noqeri framework: ")+message);
}
void valid(const Matrix& x) {
    require(x.rows>0 && x.cols>0 && x.rows<=2048 && x.cols<=2048 &&
            x.rows<=1048576/x.cols && x.values.size()==x.rows*x.cols,
            "invalid tensor shape");
    for(double v:x.values) require(std::isfinite(v) && std::abs(v)<=1e8,
                                    "nonfinite or oversized tensor value");
}
void same(const Matrix& a,const Matrix& b) {
    require(a.rows==b.rows && a.cols==b.cols,"tensor shapes differ");
}
} // namespace

bool simd_available() noexcept {
#ifdef NQAI_SSE2
    return true;
#else
    return false;
#endif
}
// Quantize GEMM operands but retain FP64 master parameters and gradients.
// FP16 is an IEEE 754 binary16 storage/rounding emulation on CPU,
// with float (FP32) accumulation. This is not hardware Tensor Core speedup.
namespace {
double cast_compute(double value,Precision precision) {
    if(precision==Precision::FP64)return value;
    if(precision==Precision::FP32)return static_cast<double>(static_cast<float>(value));
    require(precision==Precision::FP16,"unsupported compute precision");
    require(std::abs(value)<=65504,"binary16 operand overflow");
    if(value==0)return value;
    const double magnitude=std::abs(value);
    if(magnitude<std::ldexp(1.0,-25))return std::copysign(0.0,value);
    int exponent=0;std::frexp(magnitude,&exponent);
    const double step=std::ldexp(1.0,std::max(exponent-11,-24));
    const double rounded=std::nearbyint(magnitude/step)*step;
    require(rounded<=65504,"binary16 operand overflow after rounding");
    return std::copysign(rounded,value);
}
Matrix compute_gemm(const Matrix& a,const Matrix& b,Precision precision) {
    valid(a);valid(b);
    require(a.cols==b.rows && a.rows<=1048576/b.cols &&
            a.rows<=50000000/(a.cols*b.cols),"invalid compute GEMM shape");
    Matrix out(a.rows,b.cols);
    for(std::size_t i=0;i<a.rows;++i)
        for(std::size_t j=0;j<b.cols;++j) {
            float sum=0;
            for(std::size_t k=0;k<a.cols;++k) {
                const float av=static_cast<float>(
                    cast_compute(a.at(i,k),precision));
                const float bv=static_cast<float>(
                    cast_compute(b.at(k,j),precision));
                sum=static_cast<float>(sum+av*bv);
            }
            out.at(i,j)=sum;
        }
    valid(out);return out;
}
Matrix transpose_for_grad(const Matrix& m) {
    Matrix out(m.cols,m.rows);
    for(std::size_t i=0;i<m.rows;++i)
        for(std::size_t j=0;j<m.cols;++j)out.at(j,i)=m.at(i,j);
    return out;
}
} // namespace

Matrix gemm(const Matrix& a,const Matrix& b,Backend backend) {
    valid(a);valid(b);
    require(a.cols==b.rows && a.rows<=1048576/b.cols &&
            a.rows<=50000000/(a.cols*b.cols),"incompatible or oversized GEMM");
    if(backend==Backend::OpenCL)return gpu_gemm(a,b);
    Matrix c(a.rows,b.cols);
    if(backend==Backend::Auto) backend=simd_available()?Backend::Simd:Backend::Tiled;
    if(backend==Backend::Simd && !simd_available())
        throw std::runtime_error("Noqeri framework: SIMD backend unavailable");
    if(backend==Backend::Scalar) {
        for(std::size_t i=0;i<a.rows;++i)
            for(std::size_t j=0;j<b.cols;++j)
                for(std::size_t k=0;k<a.cols;++k)
                    c.values[i*b.cols+j]+=a.values[i*a.cols+k]*b.values[k*b.cols+j];
    }else{
        constexpr std::size_t block=32;
        for(std::size_t ii=0;ii<a.rows;ii+=block)
            for(std::size_t kk=0;kk<a.cols;kk+=block)
                for(std::size_t jj=0;jj<b.cols;jj+=block)
                    for(std::size_t i=ii;i<std::min(ii+block,a.rows);++i)
                        for(std::size_t k=kk;k<std::min(kk+block,a.cols);++k) {
                            const double x=a.values[i*a.cols+k];
                            std::size_t j=jj;
#ifdef NQAI_SSE2
                            if(backend==Backend::Simd) {
                                const __m128d broadcast=_mm_set1_pd(x);
                                for(;j+1<std::min(jj+block,b.cols);j+=2) {
                                    const __m128d y=_mm_loadu_pd(&b.values[k*b.cols+j]);
                                    const __m128d sum=_mm_loadu_pd(&c.values[i*b.cols+j]);
                                    _mm_storeu_pd(&c.values[i*b.cols+j],
                                                  _mm_add_pd(sum,_mm_mul_pd(broadcast,y)));
                                }
                            }
#endif
                            for(;j<std::min(jj+block,b.cols);++j)
                                c.values[i*b.cols+j]+=x*b.values[k*b.cols+j];
                        }
    }
    valid(c);
    return c;
}
std::size_t Tape::append(Node node) {
    require(nodes_.size()<128,"autodiff graph budget exceeded");
    valid(node.value);
    node.grad=Matrix(node.value.rows,node.value.cols);
    nodes_.push_back(std::move(node));
    return nodes_.size()-1;
}
std::size_t Tape::size() const noexcept {return nodes_.size();}
void Tape::set_precision(Precision precision) {
    require(precision==Precision::FP64 || precision==Precision::FP32 ||
            precision==Precision::FP16,"invalid compute precision");
    precision_=precision;
}
void Tape::set_backend(Backend backend) {
    require(backend==Backend::Auto || backend==Backend::Scalar ||
            backend==Backend::Tiled || backend==Backend::Simd ||
            backend==Backend::OpenCL,"unknown tensor backend");
    backend_=backend;
}
const Matrix& Tape::value(std::size_t id) const {
    require(id<nodes_.size(),"invalid tensor handle");
    return nodes_[id].value;
}
const Matrix& Tape::grad(std::size_t id) const {
    require(id<nodes_.size(),"invalid gradient handle");
    return nodes_[id].grad;
}
std::size_t Tape::leaf(const Matrix& value) {
    Node n;n.value=value;return append(std::move(n));
}
std::size_t Tape::add(std::size_t a,std::size_t b) {
    const Matrix& x=value(a);const Matrix& y=value(b);
    same(x,y);Node n;n.op=Op::Add;n.a=a;n.b=b;n.value=Matrix(x.rows,x.cols);
    for(std::size_t j=0;j<x.values.size();++j)n.value.values[j]=x.values[j]+y.values[j];
    return append(std::move(n));
}
std::size_t Tape::matmul(std::size_t a,std::size_t b) {
    Node n;n.op=Op::Matmul;n.a=a;n.b=b;
    require(backend_!=Backend::OpenCL || precision_==Precision::FP32,
            "OpenCL training uses FP32 compute only");
    n.value=(precision_==Precision::FP64 || backend_==Backend::OpenCL)
        ?gemm(value(a),value(b),backend_)
        :compute_gemm(value(a),value(b),precision_);
    return append(std::move(n));
}
std::size_t Tape::transpose(std::size_t a) {
    const Matrix& x=value(a);Node n;n.op=Op::Transpose;n.a=a;n.value=Matrix(x.cols,x.rows);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j)n.value.at(j,i)=x.at(i,j);
    return append(std::move(n));
}
std::size_t Tape::scale(std::size_t a,double factor) {
    require(std::isfinite(factor) && std::abs(factor)<=1e6,"invalid scale factor");
    const Matrix& x=value(a);Node n;n.op=Op::Scale;n.a=a;n.factor=factor;n.value=x;
    for(double& z:n.value.values)z*=factor;
    return append(std::move(n));
}
std::size_t Tape::tanh(std::size_t a) {
    Node n;n.op=Op::Tanh;n.a=a;n.value=value(a);
    for(double& z:n.value.values)z=std::tanh(z);
    return append(std::move(n));
}
std::size_t Tape::relu(std::size_t a) {
    Node n;n.op=Op::Relu;n.a=a;n.value=matrix::apply_relu(value(a));
    return append(std::move(n));
}
std::size_t Tape::gelu(std::size_t a) {
    Node n;n.op=Op::Gelu;n.a=a;n.value=matrix::gelu(value(a));
    return append(std::move(n));
}
std::size_t Tape::layer_norm(std::size_t a,double epsilon) {
    require(std::isfinite(epsilon) && epsilon>=1e-8 && epsilon<=1,
            "invalid layer norm epsilon");
    Node n;n.op=Op::LayerNorm;n.a=a;n.factor=epsilon;
    n.value=matrix::layer_norm_rows(value(a),epsilon);
    return append(std::move(n));
}
std::size_t Tape::softmax(std::size_t a,bool causal) {
    const Matrix& x=value(a);
    if(causal)require(x.rows==x.cols && x.rows<=256,"causal attention needs square bounded scores");
    Node n;n.op=Op::Softmax;n.a=a;n.causal=causal;n.value=Matrix(x.rows,x.cols);
    for(std::size_t i=0;i<x.rows;++i) {
        const std::size_t limit=causal?i+1:x.cols;
        double top=-std::numeric_limits<double>::infinity();
        for(std::size_t j=0;j<limit;++j)top=std::max(top,x.at(i,j));
        double denom=0;
        for(std::size_t j=0;j<limit;++j) {
            n.value.at(i,j)=std::exp(x.at(i,j)-top);
            denom+=n.value.at(i,j);
        }
        require(std::isfinite(denom) && denom>0,"unstable tensor softmax");
        for(std::size_t j=0;j<limit;++j)n.value.at(i,j)/=denom;
    }
    return append(std::move(n));
}
std::size_t Tape::gather_rows(std::size_t embedding,const std::vector<std::size_t>& ids) {
    const Matrix& x=value(embedding);
    require(!ids.empty() && ids.size()<=256,"invalid gather index length");
    Node n;n.op=Op::Gather;n.a=embedding;n.indices=ids;n.value=Matrix(ids.size(),x.cols);
    for(std::size_t i=0;i<ids.size();++i) {
        require(ids[i]<x.rows,"gather index out of bounds");
        for(std::size_t j=0;j<x.cols;++j)n.value.at(i,j)=x.at(ids[i],j);
    }
    return append(std::move(n));
}
std::size_t Tape::last_row(std::size_t a) {
    const Matrix& x=value(a);Node n;n.op=Op::LastRow;n.a=a;n.value=Matrix(1,x.cols);
    for(std::size_t j=0;j<x.cols;++j)n.value.at(0,j)=x.at(x.rows-1,j);
    return append(std::move(n));
}
std::size_t Tape::cross_entropy(std::size_t logits,std::size_t target) {
    const Matrix& x=value(logits);
    require(x.rows==1 && target<x.cols,"invalid next-token classification");
    double top=*std::max_element(x.values.begin(),x.values.end()),denom=0;
    for(double z:x.values)denom+=std::exp(z-top);
    require(std::isfinite(denom) && denom>0,"unstable cross entropy");
    Node n;n.op=Op::CrossEntropy;n.a=logits;n.target=target;
    n.value=Matrix(1,1,top+std::log(denom)-x.values[target]);
    return append(std::move(n));
}
void Tape::backward(std::size_t loss) {
    require(loss<nodes_.size() && nodes_[loss].value.values.size()==1,
            "backward requires scalar loss");
    for(auto& node:nodes_)std::fill(node.grad.values.begin(),node.grad.values.end(),0.0);
    nodes_[loss].grad.values[0]=1.0;
    for(std::size_t id=nodes_.size();id-->0;) {
        const Node& n=nodes_[id];
        switch(n.op) {
        case Op::Leaf:break;
        case Op::Add:
            for(std::size_t j=0;j<n.grad.values.size();++j) {
                nodes_[n.a].grad.values[j]+=n.grad.values[j];
                nodes_[n.b].grad.values[j]+=n.grad.values[j];
            }
            break;
        case Op::Matmul: {
            const Matrix& a=nodes_[n.a].value;const Matrix& b=nodes_[n.b].value;
            if(backend_==Backend::OpenCL) {
                const Matrix ga=gpu_gemm(n.grad,transpose_for_grad(b));
                const Matrix gb=gpu_gemm(transpose_for_grad(a),n.grad);
                for(std::size_t i=0;i<ga.values.size();++i)
                    nodes_[n.a].grad.values[i]+=ga.values[i];
                for(std::size_t i=0;i<gb.values.size();++i)
                    nodes_[n.b].grad.values[i]+=gb.values[i];
                break;
            }
            for(std::size_t i=0;i<a.rows;++i)
                for(std::size_t k=0;k<a.cols;++k)
                    for(std::size_t j=0;j<b.cols;++j) {
                        const double g=n.grad.at(i,j);
                        nodes_[n.a].grad.at(i,k)+=g*b.at(k,j);
                        nodes_[n.b].grad.at(k,j)+=g*a.at(i,k);
                    }
            break;
        }
        case Op::Transpose: {
            for(std::size_t i=0;i<n.value.rows;++i)
                for(std::size_t j=0;j<n.value.cols;++j)
                    nodes_[n.a].grad.at(j,i)+=n.grad.at(i,j);
            break;
        }
        case Op::Scale:
            for(std::size_t j=0;j<n.grad.values.size();++j)
                nodes_[n.a].grad.values[j]+=n.grad.values[j]*n.factor;
            break;
        case Op::Tanh:
            for(std::size_t j=0;j<n.grad.values.size();++j)
                nodes_[n.a].grad.values[j]+=
                    n.grad.values[j]*(1.0-n.value.values[j]*n.value.values[j]);
            break;
        case Op::Relu: {
            const Matrix& source=nodes_[n.a].value;
            for(std::size_t j=0;j<n.grad.values.size();++j)
                if(source.values[j]>0)
                    nodes_[n.a].grad.values[j]+=n.grad.values[j];
            break;
        }
        case Op::Gelu: {
            const Matrix& source=nodes_[n.a].value;
            constexpr double k=0.7978845608028654; // sqrt(2/pi)
            for(std::size_t j=0;j<n.grad.values.size();++j) {
                const double x=source.values[j];
                const double t=std::tanh(k*(x+0.044715*x*x*x));
                const double d=0.5*(1+t)+0.5*x*(1-t*t)*
                    k*(1+3*0.044715*x*x);
                nodes_[n.a].grad.values[j]+=n.grad.values[j]*d;
            }
            break;
        }
        case Op::LayerNorm: {
            const Matrix& source=nodes_[n.a].value;
            for(std::size_t i=0;i<source.rows;++i) {
                long double mean=0,m2=0,gsum=0,gysum=0;
                for(std::size_t j=0;j<source.cols;++j) {
                    const long double x=source.at(i,j);
                    const long double delta=x-mean;
                    mean+=delta/static_cast<long double>(j+1);
                    m2+=delta*(x-mean);
                    gsum+=n.grad.at(i,j);
                    gysum+=static_cast<long double>(n.grad.at(i,j))*n.value.at(i,j);
                }
                const long double count=source.cols;
                const long double inverse=1/std::sqrt(m2/count+n.factor);
                const long double mean_g=gsum/count;
                const long double mean_gy=gysum/count;
                for(std::size_t j=0;j<source.cols;++j) {
                    const long double grad=inverse*
                        (static_cast<long double>(n.grad.at(i,j))-mean_g-
                         static_cast<long double>(n.value.at(i,j))*mean_gy);
                    nodes_[n.a].grad.at(i,j)+=static_cast<double>(grad);
                }
            }
            break;
        }
        case Op::Softmax:
            for(std::size_t i=0;i<n.value.rows;++i) {
                const std::size_t limit=n.causal?i+1:n.value.cols;
                double dot=0;
                for(std::size_t j=0;j<limit;++j)
                    dot+=n.grad.at(i,j)*n.value.at(i,j);
                for(std::size_t j=0;j<limit;++j)
                    nodes_[n.a].grad.at(i,j)+=
                        n.value.at(i,j)*(n.grad.at(i,j)-dot);
            }
            break;
        case Op::Gather:
            for(std::size_t i=0;i<n.indices.size();++i)
                for(std::size_t j=0;j<n.value.cols;++j)
                    nodes_[n.a].grad.at(n.indices[i],j)+=n.grad.at(i,j);
            break;
        case Op::LastRow:
            for(std::size_t j=0;j<n.value.cols;++j)
                nodes_[n.a].grad.at(nodes_[n.a].value.rows-1,j)+=n.grad.at(0,j);
            break;
        case Op::CrossEntropy: {
            const Matrix& x=nodes_[n.a].value;
            double max=*std::max_element(x.values.begin(),x.values.end());
            double denom=0;
            for(double v:x.values)denom+=std::exp(v-max);
            for(std::size_t j=0;j<x.cols;++j) {
                const double derivative=std::exp(x.values[j]-max)/denom-
                                       (j==n.target?1.0:0.0);
                nodes_[n.a].grad.values[j]+=n.grad.values[0]*derivative;
            }
            break;
        }
        }
    }
    for(const auto& node:nodes_)
        for(double g:node.grad.values)
            require(std::isfinite(g),"nonfinite automatic gradient");
}
void sgd_update(Matrix& param,const Matrix& grad,double rate,double clip) {
    valid(param);valid(grad);same(param,grad);
    require(std::isfinite(rate) && rate>0 && rate<=0.2 &&
            std::isfinite(clip) && clip>0 && clip<=100,
            "invalid optimizer learning rate or clip");
    // Stage the whole update first. An overflowing element must not leave a
    // partially modified parameter tensor (important for training retries).
    Matrix candidate=param;
    for(std::size_t i=0;i<candidate.values.size();++i)
        candidate.values[i]-=rate*std::clamp(grad.values[i],-clip,clip);
    valid(candidate);
    param.values.swap(candidate.values);
}
} // namespace noqeri_ai::framework
