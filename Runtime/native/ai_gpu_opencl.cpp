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
// OpenCL JIT program compilation can dominate hundreds of tiny transformer
// GEMMs. Reuse one context, queue and compiled kernel per worker thread.
// Thread-local ownership avoids cross-thread clSetKernelArg races.
struct GpuSession {
    cl_device_id gpu = nullptr;
    cl_context ctx = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    GpuSession() {
        gpu=device();
        if(!gpu)throw std::runtime_error("Noqeri OpenCL: GPU device unavailable; no CPU fallback");
        try {
            cl_int status=CL_SUCCESS;
            ctx=clCreateContext(nullptr,1,&gpu,nullptr,nullptr,&status);
            checked(status,"Noqeri OpenCL: cannot create GPU context");
            if(!ctx)throw std::runtime_error("Noqeri OpenCL: null context");
            queue=clCreateCommandQueue(ctx,gpu,0,&status);
            checked(status,"Noqeri OpenCL: cannot create queue");
            if(!queue)throw std::runtime_error("Noqeri OpenCL: null queue");
            program=clCreateProgramWithSource(ctx,1,&kernel_source,nullptr,&status);
            checked(status,"Noqeri OpenCL: cannot create program");
            if(!program)throw std::runtime_error("Noqeri OpenCL: null program");
            checked(clBuildProgram(program,1,&gpu,"",nullptr,nullptr),
                    "Noqeri OpenCL: GPU kernel compilation failed");
            kernel=clCreateKernel(program,"nq_gemm",&status);
            checked(status,"Noqeri OpenCL: missing GEMM kernel");
            if(!kernel)throw std::runtime_error("Noqeri OpenCL: null kernel");
        }catch(...) {
            release();
            throw;
        }
    }
    ~GpuSession(){release();}
    GpuSession(const GpuSession&)=delete;
    GpuSession& operator=(const GpuSession&)=delete;
    void release() noexcept {
        if(kernel){clReleaseKernel(kernel);kernel=nullptr;}
        if(program){clReleaseProgram(program);program=nullptr;}
        if(queue){clReleaseCommandQueue(queue);queue=nullptr;}
        if(ctx){clReleaseContext(ctx);ctx=nullptr;}
    }
};
GpuSession& session() {
    static thread_local GpuSession instance;
    return instance;
}
} // namespace
bool gpu_available() noexcept {return device()!=nullptr;}
std::string gpu_device_description() {
    const auto gpu=device();
    if(!gpu)throw std::runtime_error("Noqeri OpenCL: no physical GPU device");
    cl_device_type type=0;
    checked(clGetDeviceInfo(gpu,CL_DEVICE_TYPE,sizeof(type),&type,nullptr),
            "Noqeri OpenCL: GPU device type query failed");
    if(!(type & CL_DEVICE_TYPE_GPU))
        throw std::runtime_error("Noqeri OpenCL: selected device is not a GPU");
    const auto info=[gpu](cl_device_info key) {
        std::size_t count=0;
        checked(clGetDeviceInfo(gpu,key,0,nullptr,&count),
                "Noqeri OpenCL: GPU device info size failed");
        if(count==0 || count>2048)
            throw std::runtime_error("Noqeri OpenCL: invalid device info length");
        std::string value(count,'\0');
        checked(clGetDeviceInfo(gpu,key,count,&value[0],nullptr),
                "Noqeri OpenCL: GPU device info fetch failed");
        while(!value.empty() && value.back()=='\0')value.pop_back();
        return value;
    };
    return "vendor="+info(CL_DEVICE_VENDOR)+";name="+info(CL_DEVICE_NAME)+
           ";driver="+info(CL_DRIVER_VERSION);
}

Matrix gpu_gemm(const Matrix& a,const Matrix& b) {
    if(!a.rows || !a.cols || !b.rows || !b.cols ||
       a.cols!=b.rows || a.rows>2048 || a.cols>2048 || b.cols>2048 ||
       a.values.size()!=a.rows*a.cols || b.values.size()!=b.rows*b.cols ||
       a.rows>1048576/b.cols || a.rows>50000000/(a.cols*b.cols))
        throw std::runtime_error("Noqeri OpenCL: invalid bounded GEMM dimensions");
    // Fail closed if a physical OpenCL GPU cannot construct its kernel.
    auto& runtime=session();
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
    cl_int status=CL_SUCCESS;
    cl_mem ab=nullptr,bb=nullptr,cb=nullptr;
    try {
        ab=clCreateBuffer(runtime.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
                          af.size()*sizeof(float),af.data(),&status);
        checked(status,"Noqeri OpenCL: allocation A failed");
        bb=clCreateBuffer(runtime.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
                          bf.size()*sizeof(float),bf.data(),&status);
        checked(status,"Noqeri OpenCL: allocation B failed");
        cb=clCreateBuffer(runtime.ctx,CL_MEM_WRITE_ONLY,
                          cf.size()*sizeof(float),nullptr,&status);
        checked(status,"Noqeri OpenCL: allocation C failed");
        const cl_int m=static_cast<cl_int>(a.rows),n=static_cast<cl_int>(b.cols),
                     k=static_cast<cl_int>(a.cols);
        checked(clSetKernelArg(runtime.kernel,0,sizeof(ab),&ab),"Noqeri OpenCL: arg A");
        checked(clSetKernelArg(runtime.kernel,1,sizeof(bb),&bb),"Noqeri OpenCL: arg B");
        checked(clSetKernelArg(runtime.kernel,2,sizeof(cb),&cb),"Noqeri OpenCL: arg C");
        checked(clSetKernelArg(runtime.kernel,3,sizeof(m),&m),"Noqeri OpenCL: arg M");
        checked(clSetKernelArg(runtime.kernel,4,sizeof(n),&n),"Noqeri OpenCL: arg N");
        checked(clSetKernelArg(runtime.kernel,5,sizeof(k),&k),"Noqeri OpenCL: arg K");
        const size_t global[]={a.rows,b.cols};
        checked(clEnqueueNDRangeKernel(runtime.queue,runtime.kernel,2,nullptr,global,
                                       nullptr,0,nullptr,nullptr),
                "Noqeri OpenCL: kernel execution failed");
        checked(clEnqueueReadBuffer(runtime.queue,cb,CL_TRUE,0,
                                    cf.size()*sizeof(float),cf.data(),0,nullptr,nullptr),
                "Noqeri OpenCL: GPU readback failed");
    } catch(...) {
        if(cb)clReleaseMemObject(cb);
        if(bb)clReleaseMemObject(bb);
        if(ab)clReleaseMemObject(ab);
        throw;
    }
    clReleaseMemObject(cb);
    clReleaseMemObject(bb);
    clReleaseMemObject(ab);
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
std::string gpu_device_description() {
    throw std::runtime_error("Noqeri OpenCL: GPU backend was not built");
}

Matrix gpu_gemm(const Matrix&,const Matrix&) {
    throw std::runtime_error("Noqeri OpenCL: unavailable (build with OpenCL SDK and GPU device)");
}
#endif
} // namespace noqeri_ai::framework
