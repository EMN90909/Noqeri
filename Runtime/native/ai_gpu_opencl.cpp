// SPDX-License-Identifier: Apache-2.0
// Explicit OpenCL GPU-only fp32 training GEMM (forward and backward GEMMs).
#include "noqeri/ai_framework.hpp"
#include <cmath>
#include <stdexcept>
#include <vector>
#ifdef NQAI_HAVE_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif
namespace noqeri_ai::framework {
#ifdef NQAI_HAVE_OPENCL
namespace {
cl_device_id device() noexcept {
    cl_uint platforms_count=0;
    if(clGetPlatformIDs(0,nullptr,&platforms_count)!=CL_SUCCESS || !platforms_count)
        return nullptr;
    std::vector<cl_platform_id> platforms(platforms_count);
    if(clGetPlatformIDs(platforms_count,platforms.data(),nullptr)!=CL_SUCCESS)
        return nullptr;
    for(auto platform:platforms) {
        cl_device_id gpu=nullptr;
        if(clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,1,&gpu,nullptr)==CL_SUCCESS)
            return gpu;
    }
    return nullptr;
}
void checked(cl_int status,const char* reason) {
    if(status!=CL_SUCCESS)throw std::runtime_error(reason);
}
const char* kernel_source=R"CLC(
__kernel void nq_gemm(__global const float* a,
                     __global const float* b,
                     __global float* c,
                     const int m,const int n,const int k) {
    int row=get_global_id(0),col=get_global_id(1);
    if(row>=m || col>=n)return;
    float total=0.0f;
    for(int x=0;x<k;++x)total+=a[row*k+x]*b[x*n+col];
    c[row*n+col]=total;
}
)CLC";
} // namespace
bool gpu_available() noexcept {return device()!=nullptr;}
Matrix gpu_gemm(const Matrix& a,const Matrix& b) {
    if(!a.rows || !a.cols || !b.rows || !b.cols ||
       a.cols!=b.rows || a.rows>2048 || a.cols>2048 || b.cols>2048 ||
       a.values.size()!=a.rows*a.cols || b.values.size()!=b.rows*b.cols ||
       a.rows>1048576/b.cols || a.rows>50000000/(a.cols*b.cols))
        throw std::runtime_error("Noqeri OpenCL: invalid bounded GEMM dimensions");
    auto gpu=device();
    if(!gpu)throw std::runtime_error("Noqeri OpenCL: GPU device unavailable; no CPU fallback");
    std::vector<float> af(a.values.size()),bf(b.values.size()),cf(a.rows*b.cols);
    for(std::size_t i=0;i<af.size();++i) {
        if(!std::isfinite(a.values[i]) || std::abs(a.values[i])>1e8)
            throw std::runtime_error("Noqeri OpenCL: invalid input A");
        af[i]=static_cast<float>(a.values[i]);
    }
    for(std::size_t i=0;i<bf.size();++i) {
        if(!std::isfinite(b.values[i]) || std::abs(b.values[i])>1e8)
            throw std::runtime_error("Noqeri OpenCL: invalid input B");
        bf[i]=static_cast<float>(b.values[i]);
    }
    cl_int status=0;
    cl_context ctx=clCreateContext(nullptr,1,&gpu,nullptr,nullptr,&status);
    checked(status,"Noqeri OpenCL: cannot create GPU context");
    cl_command_queue queue=nullptr;
    cl_program program=nullptr;
    cl_kernel kernel=nullptr;
    cl_mem ab=nullptr,bb=nullptr,cb=nullptr;
    try {
        queue=clCreateCommandQueue(ctx,gpu,0,&status);
        checked(status,"Noqeri OpenCL: cannot create queue");
        program=clCreateProgramWithSource(ctx,1,&kernel_source,nullptr,&status);
        checked(status,"Noqeri OpenCL: cannot create program");
        checked(clBuildProgram(program,1,&gpu,"",nullptr,nullptr),
                "Noqeri OpenCL: GPU kernel compilation failed");
        kernel=clCreateKernel(program,"nq_gemm",&status);
        checked(status,"Noqeri OpenCL: missing GEMM kernel");
        ab=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
                          af.size()*sizeof(float),af.data(),&status);
        checked(status,"Noqeri OpenCL: allocation A failed");
        bb=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
                          bf.size()*sizeof(float),bf.data(),&status);
        checked(status,"Noqeri OpenCL: allocation B failed");
        cb=clCreateBuffer(ctx,CL_MEM_WRITE_ONLY,
                          cf.size()*sizeof(float),nullptr,&status);
        checked(status,"Noqeri OpenCL: allocation C failed");
        const cl_int m=static_cast<cl_int>(a.rows),n=static_cast<cl_int>(b.cols),
                     k=static_cast<cl_int>(a.cols);
        checked(clSetKernelArg(kernel,0,sizeof(ab),&ab),"Noqeri OpenCL: arg A");
        checked(clSetKernelArg(kernel,1,sizeof(bb),&bb),"Noqeri OpenCL: arg B");
        checked(clSetKernelArg(kernel,2,sizeof(cb),&cb),"Noqeri OpenCL: arg C");
        checked(clSetKernelArg(kernel,3,sizeof(m),&m),"Noqeri OpenCL: arg M");
        checked(clSetKernelArg(kernel,4,sizeof(n),&n),"Noqeri OpenCL: arg N");
        checked(clSetKernelArg(kernel,5,sizeof(k),&k),"Noqeri OpenCL: arg K");
        const size_t global[]={a.rows,b.cols};
        checked(clEnqueueNDRangeKernel(queue,kernel,2,nullptr,global,
                                       nullptr,0,nullptr,nullptr),
                "Noqeri OpenCL: kernel execution failed");
        checked(clEnqueueReadBuffer(queue,cb,CL_TRUE,0,cf.size()*sizeof(float),
                                    cf.data(),0,nullptr,nullptr),
                "Noqeri OpenCL: GPU readback failed");
    }catch(...) {
        if(cb)clReleaseMemObject(cb);
        if(bb)clReleaseMemObject(bb);
        if(ab)clReleaseMemObject(ab);
        if(kernel)clReleaseKernel(kernel);
        if(program)clReleaseProgram(program);
        if(queue)clReleaseCommandQueue(queue);
        clReleaseContext(ctx);throw;
    }
    clReleaseMemObject(cb);clReleaseMemObject(bb);clReleaseMemObject(ab);
    clReleaseKernel(kernel);clReleaseProgram(program);
    clReleaseCommandQueue(queue);clReleaseContext(ctx);
    Matrix result(a.rows,b.cols);
    for(std::size_t i=0;i<cf.size();++i) {
        if(!std::isfinite(cf[i]) || std::abs(cf[i])>1e8)
            throw std::runtime_error("Noqeri OpenCL: invalid GPU result");
        result.values[i]=cf[i];
    }
    return result;
}
#else
bool gpu_available() noexcept {return false;}
Matrix gpu_gemm(const Matrix&,const Matrix&) {
    throw std::runtime_error("Noqeri OpenCL: unavailable (build with OpenCL SDK and GPU device)");
}
#endif
} // namespace noqeri_ai::framework
