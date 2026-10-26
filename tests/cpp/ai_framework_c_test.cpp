// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework_c.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <algorithm>
#include <cstdint>

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

    // Deterministic BPE, capacity safety and bounded CPU transformer.
    const std::string corpus="hello world\nhello world\nhello world\nhello world\n";
    const std::string word="hello";
    std::size_t ids[32]{},idCount=999;
    nq_ai_bpe_request bpe{
        reinterpret_cast<const std::uint8_t*>(corpus.data()),corpus.size(),
        reinterpret_cast<const std::uint8_t*>(word.data()),word.size(),
        4,ids,32,&idCount};
    if(nq_ai_bpe_encode(&bpe)!=0||!idCount||idCount>word.size())return 28;
    std::size_t original[32]{};
    std::copy_n(ids,idCount,original);
    const auto originalCount=idCount;
    if(nq_ai_bpe_encode(&bpe)!=0||idCount!=originalCount||
       !std::equal(original,original+idCount,ids))return 29;
    idCount=999;bpe.token_capacity=0;
    if(nq_ai_bpe_encode(&bpe)!=-3||idCount!=999)return 30;
    bpe.token_capacity=32;bpe.max_merges=33;
    if(nq_ai_bpe_encode(&bpe)!=-2||idCount!=999)return 31;
    bpe.max_merges=4;
    const std::string bad="bad\tinput";
    bpe.text=reinterpret_cast<const std::uint8_t*>(bad.data());
    bpe.text_length=bad.size();
    if(nq_ai_bpe_encode(&bpe)!=-5||idCount!=999)return 32;
    if(nq_ai_bpe_encode(nullptr)!=-1)return 33;
    std::uint8_t generated[256]{};
    std::size_t generatedLength=999;
    double trainLoss=-99;
    nq_ai_transformer_request model{
        reinterpret_cast<const std::uint8_t*>(corpus.data()),corpus.size(),
        reinterpret_cast<const std::uint8_t*>(word.data()),word.size(),
        3,4,2,2,2,0.025,42,0,generated,256,&generatedLength,&trainLoss
    };
    if(nq_ai_transformer_train_generate(&model)!=0||generatedLength==0||
       generatedLength>256||!std::isfinite(trainLoss))return 34;
    const std::string expectedGeneration(reinterpret_cast<const char*>(generated),generatedLength);
    if(nq_ai_transformer_train_generate(&model)!=0||
       std::string(reinterpret_cast<const char*>(generated),generatedLength)!=expectedGeneration)return 35;
    generatedLength=999;trainLoss=-99;generated[0]=99;model.output_capacity=1;
    if(nq_ai_transformer_train_generate(&model)!=-3||
       generatedLength!=999||trainLoss!=-99||generated[0]!=99)return 36;
    model.output_capacity=256;model.steps=0;
    if(nq_ai_transformer_train_generate(&model)!=-2||generatedLength!=999)return 37;
    if(nq_ai_transformer_train_generate(nullptr)!=-1)return 38;

    // Owned model sessions: train once, generate twice, invalidate the token.
    std::uint64_t modelHandle=99999;
    double sessionLoss=-77;
    nq_ai_model_train_request training{
        reinterpret_cast<const std::uint8_t*>(corpus.data()),corpus.size(),
        3,4,2,2,0.025,42,0,&modelHandle,&sessionLoss};
    if(nq_ai_model_train(&training)!=0 || modelHandle==0 ||
       !std::isfinite(sessionLoss))return 39;
    const auto releasedHandle=modelHandle;
    std::uint8_t sessionText[256]{};
    std::size_t sessionLen=777;
    nq_ai_model_generate_request inference{
        modelHandle,reinterpret_cast<const std::uint8_t*>(word.data()),word.size(),
        2,0.0,42,0,sessionText,256,&sessionLen};
    if(nq_ai_model_generate(&inference)!=0 || !sessionLen ||
       sessionLen>256)return 40;
    const std::string referenceText(
        reinterpret_cast<const char*>(sessionText),sessionLen);
    sessionLen=777;
    if(nq_ai_model_generate(&inference)!=0 ||
       std::string(reinterpret_cast<const char*>(sessionText),sessionLen)!=referenceText)
       return 41;
    sessionText[0]=99;sessionLen=777;inference.output_capacity=1;
    if(nq_ai_model_generate(&inference)!=-3 || sessionLen!=777 ||
       sessionText[0]!=99)return 42;
    inference.output_capacity=256;inference.backend=255;
    if(nq_ai_model_generate(&inference)!=-4 || sessionLen!=777)return 43;
    inference.backend=0;training.context=0;
    std::uint64_t previousHandle=modelHandle;
    if(nq_ai_model_train(&training)!=-2 || modelHandle!=previousHandle ||
       sessionLoss==-77)return 44;
    training.context=3;
    if(nq_ai_model_release(releasedHandle)!=0)return 45;
    if(nq_ai_model_release(releasedHandle)!=-7)return 46;
    if(nq_ai_model_generate(&inference)!=-7 || sessionLen!=777 ||
       sessionText[0]!=99)return 47;
    if(nq_ai_model_release(0)!=-7)return 48;
    std::cout<<"Native AI tensor C ABI passes\n";
    return 0;
}
