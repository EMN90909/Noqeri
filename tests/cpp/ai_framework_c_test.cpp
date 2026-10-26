// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework_c.h"
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    double a[4]={1,2,3,4}, b[4]={5,6,7,8};
    double result[4]={-99,-99,-99,-99};
    nq_ai_gemm_request request{a,2,2,b,2,2,result,4,3};
    if(nq_ai_gemm(&request)!=0) return 1;
    const double expected[4]={19,22,43,50};
    for(int i=0;i<4;++i) if(std::abs(result[i]-expected[i])>1e-12)return 2;
    result[0]=-99;
    request.output_capacity=3;
    if(nq_ai_gemm(&request)!=-3 || result[0]!=-99) return 3;
    request.output_capacity=4;
    request.backend=255;
    if(nq_ai_gemm(&request)!=-4 || result[0]!=-99) return 4;
    request.backend=3;
    request.left_cols=3;
    if(nq_ai_gemm(&request)!=-2 || result[0]!=-99) return 5;
    request.left_cols=2;
    a[0]=std::numeric_limits<double>::quiet_NaN();
    if(nq_ai_gemm(&request)!=-5 || result[0]!=-99) return 6;
    if(nq_ai_gemm(nullptr)!=-1)return 7;
    std::cout<<"Native AI GEMM C ABI passes\n";
    return 0;
}
