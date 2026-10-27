// SPDX-License-Identifier: Apache-2.0
// Bounded f64 dense linear algebra and numerically stable ML operations.
#include "noqeri/matrix.hpp"
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace noqeri_ai::matrix {
namespace {
constexpr std::size_t max_elements=1048576;
void require(bool ok,const char* why) {
    if(!ok) throw std::invalid_argument(std::string("Noqeri matrix: ")+why);
}
void valid(const Matrix& x) {
    require(x.rows && x.cols && x.rows<=2048 && x.cols<=2048 &&
            x.rows<=max_elements/x.cols && x.values.size()==x.rows*x.cols,
            "invalid shape or allocation");
    for(double v:x.values)
        require(std::isfinite(v) && std::abs(v)<=1e8,"nonfinite or oversized element");
}
Matrix checked(Matrix out) { valid(out);return out; }
std::size_t groups(const Matrix& x,std::size_t axis) {
    require(axis<=1,"axis must be 0 or 1");
    return axis==0?x.cols:x.rows;
}
double element(const Matrix& x,std::size_t axis,std::size_t group,std::size_t j) {
    return axis==0?x.at(j,group):x.at(group,j);
}
struct LogRow { double top, log_denom; };
LogRow log_row(const Matrix& x,std::size_t axis,std::size_t group) {
    const std::size_t count=axis==0?x.rows:x.cols;
    double top=element(x,axis,group,0);
    for(std::size_t i=1;i<count;++i)top=std::max(top,element(x,axis,group,i));
    long double denom=0;
    for(std::size_t i=0;i<count;++i)
        denom+=std::exp(element(x,axis,group,i)-top);
    require(denom>0 && std::isfinite(denom),"invalid log-sum-exp");
    return {top,std::log(static_cast<double>(denom))};
}
void set(Matrix& out,std::size_t axis,std::size_t group,std::size_t j,double v) {
    if(axis==0)out.at(j,group)=v;
    else out.at(group,j)=v;
}
void square(const Matrix& x) {
    valid(x);
    require(x.rows==x.cols && x.rows<=128,"square matrix limited to 128x128");
}
} // namespace

Matrix matmul(const Matrix& a,const Matrix& b) {
    return framework::gemm(a,b,framework::Backend::Auto);
}
Matrix diagonal(const Matrix& x) {
    valid(x);
    Matrix out(1,std::min(x.rows,x.cols));
    for(std::size_t i=0;i<out.cols;++i)out.at(0,i)=x.at(i,i);
    return out;
}
double trace(const Matrix& x) {
    valid(x);
    require(x.rows==x.cols,"trace requires square matrix");
    long double result=0;
    for(std::size_t i=0;i<x.rows;++i)result+=x.at(i,i);
    require(std::isfinite(result) && std::abs(result)<=1e8,"trace overflow");
    return static_cast<double>(result);
}
Matrix softmax(const Matrix& x,std::size_t axis) {
    valid(x);
    Matrix out(x.rows,x.cols);
    const std::size_t n=groups(x,axis),count=axis==0?x.rows:x.cols;
    for(std::size_t g=0;g<n;++g) {
        const auto row=log_row(x,axis,g);
        for(std::size_t j=0;j<count;++j)
            set(out,axis,g,j,std::exp(element(x,axis,g,j)-row.top-row.log_denom));
    }
    return checked(std::move(out));
}
Matrix log_softmax(const Matrix& x,std::size_t axis) {
    valid(x);
    Matrix out(x.rows,x.cols);
    const std::size_t n=groups(x,axis),count=axis==0?x.rows:x.cols;
    for(std::size_t g=0;g<n;++g) {
        const auto row=log_row(x,axis,g);
        for(std::size_t j=0;j<count;++j)
            set(out,axis,g,j,(element(x,axis,g,j)-row.top)-row.log_denom);
    }
    return checked(std::move(out));
}
Matrix logsumexp(const Matrix& x,std::size_t axis) {
    valid(x);
    const std::size_t n=groups(x,axis);
    Matrix out(axis==0?1:x.rows,axis==0?x.cols:1);
    for(std::size_t g=0;g<n;++g) {
        const auto row=log_row(x,axis,g);
        out.values[g]=row.top+row.log_denom;
    }
    return checked(std::move(out));
}
Matrix maximum(const Matrix& x,std::size_t axis) {
    valid(x);
    const std::size_t n=groups(x,axis),count=axis==0?x.rows:x.cols;
    Matrix out(axis==0?1:x.rows,axis==0?x.cols:1);
    for(std::size_t g=0;g<n;++g) {
        double v=element(x,axis,g,0);
        for(std::size_t i=1;i<count;++i)v=std::max(v,element(x,axis,g,i));
        out.values[g]=v;
    }
    return out;
}
Matrix minimum(const Matrix& x,std::size_t axis) {
    valid(x);
    const std::size_t n=groups(x,axis),count=axis==0?x.rows:x.cols;
    Matrix out(axis==0?1:x.rows,axis==0?x.cols:1);
    for(std::size_t g=0;g<n;++g) {
        double v=element(x,axis,g,0);
        for(std::size_t i=1;i<count;++i)v=std::min(v,element(x,axis,g,i));
        out.values[g]=v;
    }
    return out;
}
std::vector<std::size_t> argmax(const Matrix& x,std::size_t axis) {
    valid(x);
    const std::size_t n=groups(x,axis),count=axis==0?x.rows:x.cols;
    std::vector<std::size_t> result(n,0);
    for(std::size_t g=0;g<n;++g)
        for(std::size_t i=1;i<count;++i)
            if(element(x,axis,g,i)>element(x,axis,g,result[g]))result[g]=i;
    return result;
}
Matrix layer_norm_rows(const Matrix& x,double epsilon) {
    valid(x);
    require(std::isfinite(epsilon) && epsilon>0 && epsilon<=1,
            "invalid layer normalization epsilon");
    Matrix out(x.rows,x.cols);
    for(std::size_t i=0;i<x.rows;++i) {
        long double mean=0,m2=0;
        for(std::size_t j=0;j<x.cols;++j) {
            const long double value=x.at(i,j);
            const long double delta=value-mean;
            mean+=delta/static_cast<long double>(j+1);
            m2+=delta*(value-mean);
        }
        const long double divisor=std::sqrt(m2/x.cols+epsilon);
        for(std::size_t j=0;j<x.cols;++j)
            out.at(i,j)=static_cast<double>((x.at(i,j)-mean)/divisor);
    }
    return checked(std::move(out));
}
Matrix gelu(const Matrix& x) {
    valid(x);
    Matrix out=x;
    constexpr double k=0.7978845608028654; // sqrt(2/pi), tanh approximation
    for(double& v:out.values) {
        const double z=k*(v+0.044715*v*v*v);
        v=0.5*v*(1+std::tanh(z));
    }
    return checked(std::move(out));
}
Matrix softplus(const Matrix& x) {
    valid(x);
    Matrix out=x;
    for(double& v:out.values)
        v=std::max(v,0.0)+std::log1p(std::exp(-std::abs(v)));
    return checked(std::move(out));
}
double l1_norm(const Matrix& x) {
    valid(x);
    long double total=0;
    for(double v:x.values)total+=std::abs(v);
    require(std::isfinite(total) && total<=1e8,"L1 norm overflow");
    return static_cast<double>(total);
}
Matrix gather_rows(const Matrix& x,const std::vector<std::size_t>& indices) {
    valid(x);
    require(!indices.empty() && indices.size()<=2048 &&
            indices.size()<=max_elements/x.cols,"invalid gather length");
    Matrix out(indices.size(),x.cols);
    for(std::size_t i=0;i<indices.size();++i) {
        require(indices[i]<x.rows,"gather row out of bounds");
        std::copy_n(x.values.begin()+indices[i]*x.cols,x.cols,
                    out.values.begin()+i*out.cols);
    }
    return out;
}
Matrix gather_columns(const Matrix& x,const std::vector<std::size_t>& indices) {
    valid(x);
    require(!indices.empty() && indices.size()<=2048 &&
            x.rows<=max_elements/indices.size(),"invalid gather length");
    Matrix out(x.rows,indices.size());
    for(std::size_t j=0;j<indices.size();++j)
        require(indices[j]<x.cols,"gather column out of bounds");
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<indices.size();++j)
            out.at(i,j)=x.at(i,indices[j]);
    return out;
}
Matrix solve(const Matrix& coefficient,const Matrix& rhs) {
    square(coefficient); valid(rhs);
    const std::size_t n=coefficient.rows,m=rhs.cols;
    require(rhs.rows==n,"RHS row count differs");
    Matrix a=coefficient,x=rhs;
    double scale=0;
    for(double v:a.values)scale=std::max(scale,std::abs(v));
    require(scale>0,"singular linear system");
    // Partial pivoting, with a scale-relative threshold for near-singularity.
    for(std::size_t k=0;k<n;++k) {
        std::size_t pivot=k;
        for(std::size_t i=k+1;i<n;++i)
            if(std::abs(a.at(i,k))>std::abs(a.at(pivot,k)))pivot=i;
        require(std::abs(a.at(pivot,k))>scale*1e-12,
                "singular or ill-conditioned linear system");
        if(pivot!=k) {
            for(std::size_t j=0;j<n;++j)std::swap(a.at(k,j),a.at(pivot,j));
            for(std::size_t j=0;j<m;++j)std::swap(x.at(k,j),x.at(pivot,j));
        }
        for(std::size_t i=k+1;i<n;++i) {
            const double factor=a.at(i,k)/a.at(k,k);
            a.at(i,k)=0;
            for(std::size_t j=k+1;j<n;++j)a.at(i,j)-=factor*a.at(k,j);
            for(std::size_t j=0;j<m;++j)x.at(i,j)-=factor*x.at(k,j);
        }
    }
    for(std::size_t i=n;i-->0;) {
        for(std::size_t j=0;j<m;++j) {
            long double z=x.at(i,j);
            for(std::size_t k=i+1;k<n;++k)z-=a.at(i,k)*x.at(k,j);
            x.at(i,j)=static_cast<double>(z/a.at(i,i));
        }
    }
    return checked(std::move(x));
}
Matrix inverse(const Matrix& x) {
    square(x);
    return solve(x,identity(x.rows));
}
Matrix cholesky(const Matrix& x) {
    square(x);
    const std::size_t n=x.rows;
    double scale=0;
    for(std::size_t i=0;i<n;++i)scale=std::max(scale,std::abs(x.at(i,i)));
    require(scale>0,"matrix is not positive definite");
    for(std::size_t i=0;i<n;++i)
        for(std::size_t j=i+1;j<n;++j)
            require(std::abs(x.at(i,j)-x.at(j,i))<=
                1e-10*std::max({1.0,std::abs(x.at(i,j)),std::abs(x.at(j,i))}),
                "Cholesky needs symmetric input");
    Matrix l(n,n);
    for(std::size_t i=0;i<n;++i)
        for(std::size_t j=0;j<=i;++j) {
            long double v=x.at(i,j);
            for(std::size_t k=0;k<j;++k)v-=static_cast<long double>(l.at(i,k))*l.at(j,k);
            if(i==j) {
                require(v>scale*1e-12,"matrix is not positive definite");
                l.at(i,j)=std::sqrt(static_cast<double>(v));
            }else l.at(i,j)=static_cast<double>(v/l.at(j,j));
        }
    return checked(std::move(l));
}
Matrix covariance_columns(const Matrix& x) {
    valid(x);
    require(x.rows>=2 && x.cols<=128 &&
            x.cols<=50000000/x.cols &&
            x.rows<=50000000/(x.cols*x.cols),"covariance workload too large");
    std::vector<long double> means(x.cols,0);
    for(std::size_t j=0;j<x.cols;++j)
        for(std::size_t i=0;i<x.rows;++i)
            means[j]+=static_cast<long double>(x.at(i,j))/x.rows;
    Matrix out(x.cols,x.cols);
    for(std::size_t j=0;j<x.cols;++j)
        for(std::size_t k=j;k<x.cols;++k) {
            long double acc=0;
            for(std::size_t i=0;i<x.rows;++i)
                acc+=(static_cast<long double>(x.at(i,j))-means[j])*
                     (static_cast<long double>(x.at(i,k))-means[k]);
            out.at(j,k)=out.at(k,j)=static_cast<double>(acc/(x.rows-1));
        }
    return checked(std::move(out));
}
} // namespace noqeri_ai::matrix
