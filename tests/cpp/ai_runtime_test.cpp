// SPDX-License-Identifier: GPL-3.0-only
#include "noqeri/ai.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
using namespace noqeri_ai;
int main(int argc,char** argv) {
    assert(argc==2);
    const fs::path root=argv[1];
    fs::create_directories(root);
    std::vector<Sample> rows;
    for(int i=-40;i<40;++i)rows.push_back({{double(i)/10.0},i>=0?1.0:0.0});
    Options options;options.seed=11;options.epochs=200;options.learning_rate=0.09;
    TrainingReport report;
    const auto model=train(rows,options,&report);
    assert(report.validation_accuracy>=0.85);
    assert(report.validation_loss<0.5);
    assert(predict_probability(model,{3.0})>0.9);
    assert(predict_probability(model,{-3.0})<0.1);
    auto repeat=train(rows,options);
    assert(repeat.weights==model.weights && repeat.bias==model.bias);
    const auto file=root/"model.nqai";
    save_model(model,file.string());
    const auto restored=load_model(file.string());
    assert(std::abs(predict_probability(restored,{1.0})-
                    predict_probability(model,{1.0}))<1e-12);
    bool bad=false;
    try{predict_probability(model,{1.0,2.0});}catch(...){bad=true;}
    assert(bad);
    bad=false;try{predict_probability(model,{std::nan("")});}catch(...){bad=true;}
    assert(bad);
    bad=false;try{train(rows,Options{0,0.1,0,42});}catch(...){bad=true;}
    assert(bad);
    // Agents must refuse external tools unless the user allowed them.
    AgentPolicy policy; policy.max_steps=7;policy.max_tool_calls=7;
    std::size_t invoked=0;
    const auto denied=run_agent(restored,rows,policy,
        [&](const std::string&,std::size_t,double){++invoked;return true;});
    assert(invoked==0&&denied.steps==7&&denied.denied_calls==7&&
           denied.budget_exhausted);
    policy.allow_emit=true;policy.max_tool_calls=3;
    const auto allowed=run_agent(restored,rows,policy,
        [&](const std::string& tool,std::size_t i,double probability){
            assert(i<7&&(tool=="emit_positive"||tool=="emit_negative"));
            assert(probability>=0.0&&probability<=1.0);
            ++invoked;return true;
        });
    assert(allowed.tool_calls==3&&invoked==3&&allowed.denied_calls==4&&allowed.steps==7);
    policy.max_steps=1;policy.max_tool_calls=1;
    const auto capped=run_agent(restored,rows,policy,
        [&](const std::string&,std::size_t,double){return false;});
    assert(capped.steps==1 && capped.denied_calls==1 && capped.tool_calls==0);
    bad=false;
    try{
        std::ofstream out(root/"malformed.nqai");
        out<<"NQAI1 99\n";
        out.close();
        load_model((root/"malformed.nqai").string());
    }catch(...){bad=true;}
    assert(bad);
    bad=false;try{read_csv((root/"missing.csv").string(),true);}catch(...){bad=true;}
    assert(bad);
    {
        std::ofstream out(root/"input.csv");
        out<<"1,0\n2,1\n";
    }
    assert(read_csv((root/"input.csv").string(),true).size()==2);
    bad=false;
    {
        std::ofstream out(root/"bad.csv");
        out<<"1,1\nnan,0\n";
    }
    try{read_csv((root/"bad.csv").string(),true);}catch(...){bad=true;}
    assert(bad);
    std::cout<<"PASS: local CPU training, deterministic validation, model integrity, capability budgets\n";
    return 0;
}
