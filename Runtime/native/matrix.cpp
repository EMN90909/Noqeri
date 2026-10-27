// SPDX-License-Identifier: Apache-2.0
#include "noqeri/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
namespace noqeri_ai::matrix {
namespace {
void require(bool ok, const char* why) {
    if(!ok) throw std::invalid_argument(std::string("Noqeri matrix: ")+why);
}
void valid(const Matrix& m) {
    require(m.rows>0 && m.cols>0 && m.rows<=2048 && m.cols<=2048 &&
            m.rows<=1048576/m.cols && m.values.size()==m.rows*m.cols,
            "invalid matrix shape");
    for(double v:m.values) require(std::isfinite(v) && std::abs(v)<=1e8,
                                   "invalid matrix element");
}
void same(const Matrix& a,const Matrix& b) {
    require(a.rows==b.rows && a.cols==b.cols,"shape mismatch");
}
void checked(Matrix& m) {valid(m);}
template<class F> Matrix binary(const Matrix& a,const Matrix& b,F fn) {
    valid(a);valid(b);
    // Explicit NumPy-style row/column broadcasting, without ambiguous flattening.
    require((a.rows==b.rows || a.rows==1 || b.rows==1) &&
            (a.cols==b.cols || a.cols==1 || b.cols==1),"not broadcastable");
    Matrix out(std::max(a.rows,b.rows),std::max(a.cols,b.cols));
    for(std::size_t i=0;i<out.rows;++i)
        for(std::size_t j=0;j<out.cols;++j)
            out.at(i,j)=fn(a.at(i%a.rows,j%a.cols),b.at(i%b.rows,j%b.cols));
    checked(out);return out;
}
}
Matrix zeros(std::size_t r,std::size_t c) {return Matrix(r,c);}
Matrix ones(std::size_t r,std::size_t c) {return Matrix(r,c,1.0);}
Matrix identity(std::size_t n) {
    Matrix m(n,n);
    for(std::size_t i=0;i<n;++i)m.at(i,i)=1.0;
    return m;
}
Matrix transpose(const Matrix& x) {
    valid(x);Matrix out(x.cols,x.rows);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j)out.at(j,i)=x.at(i,j);
    return out;
}
Matrix reshape(const Matrix& x,std::size_t r,std::size_t c) {
    valid(x);require(r>0 && c>0 && r<=2048 && c<=2048 &&
                     r<=1048576/c && r*c==x.values.size(),"invalid reshape");
    Matrix out=x;out.rows=r;out.cols=c;return out;
}
Matrix slice(const Matrix& x,std::size_t rb,std::size_t re,
             std::size_t cb,std::size_t ce) {
    valid(x);require(rb<re && re<=x.rows && cb<ce && ce<=x.cols,"invalid slice");
    Matrix out(re-rb,ce-cb);
    for(std::size_t i=0;i<out.rows;++i)
        for(std::size_t j=0;j<out.cols;++j)out.at(i,j)=x.at(rb+i,cb+j);
    return out;
}
Matrix concatenate_rows(const Matrix& a,const Matrix& b) {
    valid(a);valid(b);require(a.cols==b.cols && a.rows+b.rows<=2048 &&
      a.rows+b.rows<=1048576/a.cols,"row concatenation exceeds limits");
    Matrix out(a.rows+b.rows,a.cols);
    std::copy(a.values.begin(),a.values.end(),out.values.begin());
    std::copy(b.values.begin(),b.values.end(),out.values.begin()+a.values.size());
    return out;
}
Matrix concatenate_cols(const Matrix& a,const Matrix& b) {
    valid(a);valid(b);require(a.rows==b.rows && a.cols+b.cols<=2048 &&
      a.rows<=1048576/(a.cols+b.cols),"column concatenation exceeds limits");
    Matrix out(a.rows,a.cols+b.cols);
    for(std::size_t i=0;i<a.rows;++i) {
        std::copy_n(a.values.begin()+i*a.cols,a.cols,out.values.begin()+i*out.cols);
        std::copy_n(b.values.begin()+i*b.cols,b.cols,out.values.begin()+i*out.cols+a.cols);
    }
    return out;
}
Matrix add(const Matrix& a,const Matrix& b) {return binary(a,b,[](double x,double y){return x+y;});}
Matrix subtract(const Matrix& a,const Matrix& b) {return binary(a,b,[](double x,double y){return x-y;});}
Matrix multiply(const Matrix& a,const Matrix& b) {return binary(a,b,[](double x,double y){return x*y;});}
Matrix divide(const Matrix& a,const Matrix& b) {
    return binary(a,b,[](double x,double y){
        require(y!=0.0,"division by zero");return x/y;
    });
}
Matrix scale(const Matrix& x,double s) {
    valid(x);require(std::isfinite(s),"invalid scalar");
    Matrix out=x;for(double& v:out.values)v*=s;checked(out);return out;
}
Matrix apply_relu(const Matrix& x) {
    valid(x);Matrix out=x;
    for(double& v:out.values)v=std::max(0.0,v);return out;
}
Matrix sigmoid(const Matrix& x) {
    valid(x);Matrix out=x;
    for(double& v:out.values) {
        if(v>=0) {const double e=std::exp(-v);v=1.0/(1.0+e);}
        else {const double e=std::exp(v);v=e/(1.0+e);}
    }
    return out;
}
Matrix clip(const Matrix& x,double lo,double hi) {
    valid(x);require(std::isfinite(lo)&&std::isfinite(hi)&&lo<=hi,"invalid clip");
    Matrix out=x;for(double& v:out.values)v=std::clamp(v,lo,hi);return out;
}
Matrix sum(const Matrix& x,std::size_t axis) {
    valid(x);require(axis<=1,"axis must be 0 or 1");
    Matrix out(axis==0?1:x.rows,axis==0?x.cols:1);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j)
            out.at(axis==0?0:i,axis==0?j:0)+=x.at(i,j);
    checked(out);return out;
}
Matrix mean(const Matrix& x,std::size_t axis) {
    Matrix out=sum(x,axis);const double count=double(axis==0?x.rows:x.cols);
    for(double& v:out.values)v/=count;return out;
}
Matrix variance(const Matrix& x,std::size_t axis) {
    valid(x);const Matrix mu=mean(x,axis);
    Matrix out(mu.rows,mu.cols);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j) {
            const double d=x.at(i,j)-mu.at(axis==0?0:i,axis==0?j:0);
            out.at(axis==0?0:i,axis==0?j:0)+=d*d;
        }
    const double count=double(axis==0?x.rows:x.cols);
    for(double& v:out.values)v/=count;
    checked(out);return out;
}
Matrix normalize_columns(const Matrix& x,double epsilon) {
    valid(x);require(std::isfinite(epsilon)&&epsilon>0,"invalid epsilon");
    Matrix mu=mean(x,0),var=variance(x,0),out(x.rows,x.cols);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j)
            out.at(i,j)=(x.at(i,j)-mu.at(0,j))/std::sqrt(var.at(0,j)+epsilon);
    checked(out);return out;
}
Matrix conv2d_valid(const Matrix& input,const Matrix& kernel,std::size_t stride) {
    valid(input);valid(kernel);
    require(stride>0 && kernel.rows<=input.rows && kernel.cols<=input.cols,
            "invalid convolution dimensions or stride");
    const std::size_t r=1+(input.rows-kernel.rows)/stride;
    const std::size_t c=1+(input.cols-kernel.cols)/stride;
    Matrix out(r,c);
    for(std::size_t i=0;i<r;++i)
        for(std::size_t j=0;j<c;++j) {
            double acc=0;
            for(std::size_t ki=0;ki<kernel.rows;++ki)
                for(std::size_t kj=0;kj<kernel.cols;++kj)
                    acc+=input.at(i*stride+ki,j*stride+kj)*kernel.at(ki,kj);
            out.at(i,j)=acc;
        }
    checked(out);return out;
}
double dot(const Matrix& a,const Matrix& b) {
    valid(a);valid(b);require(a.values.size()==b.values.size(),"dot sizes differ");
    double out=0;
    for(std::size_t i=0;i<a.values.size();++i)out+=a.values[i]*b.values[i];
    require(std::isfinite(out),"dot overflow");return out;
}
double l2_norm(const Matrix& x) {
    valid(x);double norm=0;
    for(double v:x.values)norm=std::hypot(norm,v);
    require(std::isfinite(norm),"norm overflow");return norm;
}
} // namespace noqeri_ai::matrix
