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

    double scores[4]={0,0,1,1}, probabilities[4]={-99,-99,-99,-99};
    nq_ai_softmax_request softmax{scores,2,2,probabilities,4,0};
    if(nq_ai_softmax(&softmax)!=0)return 8;
    for(double v:probabilities)if(std::abs(v-0.5)>1e-12)return 9;
    probabilities[0]=-99;softmax.output_capacity=3;
    if(nq_ai_softmax(&softmax)!=-3||probabilities[0]!=-99)return 10;
    softmax.output_capacity=4;softmax.causal=1;
    if(nq_ai_softmax(&softmax)!=0||probabilities[0]!=1)return 11;
    softmax.causal=2;probabilities[0]=-99;
    if(nq_ai_softmax(&softmax)!=-4||probabilities[0]!=-99)return 12;
    softmax.causal=0;scores[0]=std::numeric_limits<double>::quiet_NaN();
    if(nq_ai_softmax(&softmax)!=-5||probabilities[0]!=-99)return 13;
    double p[2]={1,2},g[2]={2,-4},updated[2]={-99,-99};
    nq_ai_sgd_request sgd{p,g,2,0.1,1.0,updated,2};
    if(nq_ai_sgd(&sgd)!=0||std::abs(updated[0]-0.9)>1e-12||
       std::abs(updated[1]-2.1)>1e-12)return 14;
    if(p[0]!=1||p[1]!=2)return 15;
    updated[0]=-99;sgd.output_capacity=1;
    if(nq_ai_sgd(&sgd)!=-3||updated[0]!=-99)return 16;
    sgd.output_capacity=2;sgd.learning_rate=0;
    if(nq_ai_sgd(&sgd)!=-4||updated[0]!=-99)return 17;
    sgd.learning_rate=0.1;g[0]=std::numeric_limits<double>::infinity();
    if(nq_ai_sgd(&sgd)!=-5||updated[0]!=-99)return 18;
    if(nq_ai_softmax(nullptr)!=-1||nq_ai_sgd(nullptr)!=-1)return 19;

    // Stateful graph objects never outlive the C call. Verify reverse-mode
    // derivative and rejection without partially modifying output buffers.
    double logits[2]={2,1}, lossOutput[1]={-99}, lossGrad[2]={-99,-99};
    nq_ai_tape_step steps[2]{};
    steps[0].operation=0;
    steps[0].data=logits;
    steps[0].rows=1;steps[0].cols=2;
    steps[1].operation=7;
    steps[1].a=0;steps[1].target=0;
    std::size_t outRows=999,outCols=999;
    nq_ai_tape_request tape{steps,2,1,1,0,1,0,lossOutput,1,
                             lossGrad,2,&outRows,&outCols};
    if(nq_ai_tape_execute(&tape)!=0)return 21;
    if(outRows!=1||outCols!=1||
       std::abs(lossOutput[0]-0.3132616875)>1e-6||
       std::abs(lossGrad[0]+0.2689414214)>1e-6||
       std::abs(lossGrad[1]-0.2689414214)>1e-6)return 22;
    lossOutput[0]=-99;lossGrad[0]=-99;outRows=999;outCols=999;
    tape.gradient_capacity=1;
    if(nq_ai_tape_execute(&tape)!=-3||lossOutput[0]!=-99||
       lossGrad[0]!=-99||outRows!=999||outCols!=999)return 23;
    tape.gradient_capacity=2;
    steps[1].a=1; // forbidden forward/self reference
    if(nq_ai_tape_execute(&tape)!=-4||lossOutput[0]!=-99)return 24;
    steps[1].a=0;steps[0].rows=0;
    if(nq_ai_tape_execute(&tape)!=-2||lossOutput[0]!=-99)return 25;
    steps[0].rows=1;logits[0]=std::numeric_limits<double>::quiet_NaN();
    if(nq_ai_tape_execute(&tape)!=-5||lossOutput[0]!=-99)return 26;
    if(nq_ai_tape_execute(nullptr)!=-1)return 27;
    std::cout<<"Native AI tensor C ABI passes\n";
    return 0;
}
