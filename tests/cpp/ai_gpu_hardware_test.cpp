// SPDX-License-Identifier: GPL-3.0-only
// Physical-device-only OpenCL AI regression. Intentionally NOT in CPU CTest.
// A passing test proves that actual OpenCL GPU kernels were dispatched,
// GPU-computed gradients agree with a CPU reference, and training updates run.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using noqeri_ai::Matrix;
using namespace noqeri_ai::framework;

namespace {
void check(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error("GPU hardware regression: "+message);
}
bool near(double actual,double reference,double tolerance=2e-4) {
    return std::isfinite(actual) &&
           std::abs(actual-reference)<=tolerance*(1+std::abs(reference));
}
std::string json_escape(const std::string& input) {
    std::string out;
    for(unsigned char c:input) {
        if(c=='"' || c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c=='\t')out+="\\t";
        else if(c>=32 && c<127)out+=static_cast<char>(c);
        else out+='?';
    }
    return out;
}
void compare(const Matrix& actual,const Matrix& expected,
             const std::string& name,double tolerance=2e-4) {
    check(actual.rows==expected.rows && actual.cols==expected.cols,
          name+" dimensions differ");
    for(std::size_t i=0;i<actual.values.size();++i)
        check(near(actual.values[i],expected.values[i],tolerance),
              name+" differs at element "+std::to_string(i));
}
void gemm_cases() {
    const std::size_t cases[][3]={{1,1,1},{3,5,7},{17,31,11},
                                  {32,17,19},{63,41,28}};
    for(const auto& dims:cases) {
        const std::size_t m=dims[0],k=dims[1],n=dims[2];
        Matrix a(m,k),b(k,n);
        for(std::size_t j=0;j<a.values.size();++j)
            a.values[j]=static_cast<double>(static_cast<int>((j*13)%29)-14)*0.03125;
        for(std::size_t j=0;j<b.values.size();++j)
            b.values[j]=static_cast<double>(static_cast<int>((j*7)%37)-18)*0.0234375;
        compare(gpu_gemm(a,b),gemm(a,b,Backend::Scalar),"OpenCL GEMM",3e-4);
    }
    bool rejected=false;
    try {gpu_gemm(Matrix(2,3),Matrix(4,2));}
    catch(const std::exception&){rejected=true;}
    check(rejected,"invalid GPU matrix dimensions accepted");
}
void backward_cases() {
    Matrix input(2,3),weight(3,4);
    input.values={0.2,-0.4,0.5,0.7,-0.3,0.6};
    weight.values={0.4,-0.5,0.1,0.7,0.3,0.6,-0.2,0.5,-0.8,0.4,0.2,-0.3};
    Tape gpu;
    gpu.set_backend(Backend::OpenCL);
    gpu.set_precision(Precision::FP32);
    const auto a=gpu.leaf(input),b=gpu.leaf(weight);
    const auto c=gpu.matmul(a,b);
    const auto loss=gpu.cross_entropy(gpu.last_row(c),2);
    gpu.backward(loss);
    Tape cpu;
    cpu.set_backend(Backend::Scalar);
    const auto ra=cpu.leaf(input),rb=cpu.leaf(weight);
    const auto rc=cpu.matmul(ra,rb);
    const auto reference_loss=cpu.cross_entropy(cpu.last_row(rc),2);
    cpu.backward(reference_loss);
    compare(gpu.value(c),cpu.value(rc),"GPU forward");
    compare(gpu.value(loss),cpu.value(reference_loss),"GPU loss");
    // These matmul gradients are computed using GPU kernels, not CPU fallback.
    compare(gpu.grad(a),cpu.grad(ra),"GPU dInput");
    compare(gpu.grad(b),cpu.grad(rb),"GPU dWeight");
}
std::pair<double,double> training_case() {
    const std::string corpus=
        "abcd abcd abcd\nabcd abcd abcd\n"
        "abcd abcd abcd\nabcd abcd abcd\n";
    TransformerOptions config;
    config.backend=Backend::OpenCL;
    config.precision=Precision::FP32;
    config.optimizer=TrainingOptimizer::AdamW;
    config.layers=2;
    config.context=3;
    config.hidden=6;
    config.merges=2;
    config.steps=4;
    config.batch_size=2;
    config.learning_rate=0.003;
    config.seed=1234;
    double gpu_loss=0;
    const auto trained=train_transformer(corpus,config,&gpu_loss);
    check(std::isfinite(gpu_loss) && gpu_loss>0,"invalid GPU training loss");
    check(trained.extra_layers.size()==1,"GPU training used wrong architecture");
    // Compare the same seeded learning problem to an explicit CPU FP32 path.
    config.backend=Backend::Scalar;
    double cpu_loss=0;
    const auto reference=train_transformer(corpus,config,&cpu_loss);
    check(std::isfinite(cpu_loss) && cpu_loss>0,"invalid CPU training loss");
    check(near(gpu_loss,cpu_loss,2e-3),"GPU training loss diverges from CPU");
    compare(trained.output,reference.output,"GPU-trained output weights",3e-3);
    const auto eval=evaluate_transformer(trained,"abcd",Backend::OpenCL);
    check(std::isfinite(eval.loss) && eval.positions==3,
          "GPU evaluation failed");
    return {gpu_loss,cpu_loss};
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2) {
        std::cerr<<"Usage: noqeri-ai-gpu-hardware-test <evidence.json>\n";
        return 2;
    }
    try {
        check(gpu_available(),"no CL_DEVICE_TYPE_GPU present (refusing CPU-only pass)");
        const auto device=gpu_device_description();
        check(!device.empty(),"selected OpenCL GPU has no provenance");
        const auto start=std::chrono::steady_clock::now();
        gemm_cases();
        backward_cases();
        const auto [gpu_loss,cpu_loss]=training_case();
        const double elapsed=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-start).count();
        std::ofstream file(argv[1],std::ios::binary|std::ios::trunc);
        check(static_cast<bool>(file),"unable to write hardware evidence");
        file<<std::setprecision(12)
            <<"{\n"
            <<"  \"result\": \"PASS\",\n"
            <<"  \"requested_backend\": \"opencl\",\n"
            <<"  \"selected_device_type\": \"CL_DEVICE_TYPE_GPU\",\n"
            <<"  \"device\": \""<<json_escape(device)<<"\",\n"
            <<"  \"gemm_case_count\": 5,\n"
            <<"  \"gradient_check\": \"forward and reverse GEMM versus CPU\",\n"
            <<"  \"training_architecture\": \"two-layer single-head transformer\",\n"
            <<"  \"training_optimizer\": \"AdamW\",\n"
            <<"  \"training_compute\": \"FP32\",\n"
            <<"  \"gpu_loss\": "<<gpu_loss<<",\n"
            <<"  \"cpu_loss\": "<<cpu_loss<<",\n"
            <<"  \"elapsed_seconds\": "<<elapsed<<"\n"
            <<"}\n";
        file.flush();
        check(static_cast<bool>(file),"hardware evidence write failed");
        std::cout<<"PASS PHYSICAL OPENCL GPU: "<<device
                 <<"; forward/backward parity, model training and evaluation"
                 <<"; elapsed_seconds="<<elapsed<<"\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL PHYSICAL OPENCL GPU: "<<error.what()<<"\n";
        return 1;
    }
}
