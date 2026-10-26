// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

namespace noqeri_ai {
namespace {
constexpr std::size_t max_features = 32;
constexpr std::size_t max_samples = 100000;
constexpr double max_feature_abs = 1e9;
namespace fs = std::filesystem;

void require(bool valid, const std::string& error) {
    if (!valid) throw std::runtime_error("Noqeri AI: " + error);
}
bool finite(double v) { return std::isfinite(v) && std::abs(v) <= max_feature_abs; }
double sigmoid(double value) {
    if (value >= 0.0) { const double e = std::exp(-value);return 1.0 / (1.0 + e); }
    const double e = std::exp(value);
    return e / (1.0 + e);
}
double loss(double p, double y) {
    p = std::max(1e-12, std::min(1.0-1e-12,p));
    return -(y*std::log(p)+(1.0-y)*std::log(1.0-p));
}
std::vector<std::string> split_csv_line(const std::string& s) {
    // Deliberately numeric CSV only: reject quoting, tab, embedded headers.
    require(s.find_first_of("\"\r") == std::string::npos, "numeric CSV quotes/CR not supported");
    std::vector<std::string> columns;
    std::stringstream input(s);
    std::string token;
    while(std::getline(input,token,','))columns.push_back(token);
    if(!s.empty() && s.back()==',')columns.emplace_back("");
    return columns;
}
double parse_double(const std::string& text) {
    require(!text.empty() && text.size()<=96, "empty or oversized numeric field");
    std::size_t consumed=0;
    double parsed=0;
    try { parsed=std::stod(text,&consumed); }
    catch(...) { throw std::runtime_error("Noqeri AI: malformed numeric input"); }
    require(consumed==text.size()&&finite(parsed),"nonfinite, oversized or nonnumeric input");
    return parsed;
}
void validate_model(const Model& model) {
    const auto count=model.weights.size();
    require(count>0&&count<=max_features&&model.mean.size()==count&&model.scale.size()==count,
            "invalid model shape");
    require(finite(model.bias),"invalid model bias");
    for(std::size_t i=0;i<count;++i) {
        require(finite(model.mean[i])&&finite(model.scale[i])&&model.scale[i]>0.0&&finite(model.weights[i]),
                "invalid model weight or normalization");
    }
}
double normalized_dot(const Model& model, const Sample& sample) {
    double sum=model.bias;
    for(std::size_t i=0;i<model.weights.size();++i)
        sum+=model.weights[i]*(sample.features[i]-model.mean[i])/model.scale[i];
    return sum;
}
}
std::vector<Sample> read_csv(const std::string& filename, bool has_label, std::size_t max_rows) {
    require(max_rows>0&&max_rows<=max_samples,"row budget must be 1..100000");
    std::ifstream input(filename,std::ios::binary);
    require(bool(input),"cannot read CSV file");
    std::vector<Sample> samples;
    std::size_t dimensions=0;
    std::size_t line_count=0;
    // Bounded input buffer: std::getline(string) would allocate arbitrarily
    // large memory before enforcing a CSV line-size limit.
    char buffer[8194]{};
    while(input.getline(buffer,sizeof(buffer))) {
        require(++line_count<=max_rows,"row budget exceeded");
        std::string line(buffer);
        if(!line.empty()&&line.back()=='\r')line.pop_back(); // Windows CRLF
        require(line.size()<=8192,"CSV line over 8192 bytes");
        require(!line.empty(),"blank CSV row");
        const auto fields=split_csv_line(line);
        require(fields.size() >= (has_label ? 2u : 1u),"insufficient CSV columns");
        const auto size=fields.size()-(has_label?1u:0u);
        require(size>=1&&size<=max_features,"feature dimension must be 1..32");
        if(dimensions==0)dimensions=size;
        require(dimensions==size,"inconsistent CSV feature dimension");
        Sample row;
        row.features.reserve(dimensions);
        for(std::size_t i=0;i<dimensions;++i)row.features.push_back(parse_double(fields[i]));
        if(has_label) {
            row.label=parse_double(fields.back());
            require(row.label==0.0||row.label==1.0,"training labels must be 0 or 1");
        }
        samples.push_back(std::move(row));
    }
    require(!input.bad() && !(input.fail()&&!input.eof()) && !samples.empty(),
            "CSV read failure, oversized line or no data");
    return samples;
}
Model train(const std::vector<Sample>& samples,const Options& options,TrainingReport* report) {
    require(samples.size()>=8&&samples.size()<=max_samples,"training needs 8..100000 rows");
    require(options.epochs>=1&&options.epochs<=10000,"epochs must be 1..10000");
    require(options.learning_rate>=1e-6&&options.learning_rate<=1.0&&std::isfinite(options.learning_rate),
            "learning rate must be within 0.000001..1");
    require(options.l2>=0.0&&options.l2<=0.1&&std::isfinite(options.l2),"l2 outside 0..0.1");
    const auto dims=samples[0].features.size();
    require(dims>0&&dims<=max_features,"invalid dimensions");
    for(const auto& row:samples) {
        require(row.features.size()==dims&& (row.label==0.0||row.label==1.0),
                "invalid labels/dimensions");
        for(auto f:row.features)require(finite(f),"invalid training feature");
    }
    std::vector<std::size_t> order(samples.size());
    std::iota(order.begin(),order.end(),0);
    std::mt19937 rng(options.seed);
    std::shuffle(order.begin(),order.end(),rng);
    const auto validation_count=std::max<std::size_t>(1,samples.size()/5);
    const auto train_count=samples.size()-validation_count;
    require(train_count>=2,"not enough training examples");
    Model model;
    model.mean.assign(dims,0.0);
    model.scale.assign(dims,1.0);
    model.weights.assign(dims,0.0);
    for(std::size_t j=0;j<dims;++j) {
        for(std::size_t i=0;i<train_count;++i)
            model.mean[j]+=samples[order[i]].features[j]/double(train_count);
        double variance=0.0;
        for(std::size_t i=0;i<train_count;++i) {
            const auto d=samples[order[i]].features[j]-model.mean[j];
            variance+=(d*d)/double(train_count);
        }
        model.scale[j]=std::max(1e-9,std::sqrt(variance));
        require(finite(model.mean[j])&&finite(model.scale[j]),"nonfinite normalization");
    }
    for(unsigned epoch=0;epoch<options.epochs;++epoch) {
        // Training partition only. Never mix validation rows into SGD.
        std::shuffle(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(train_count),rng);
        const double decay=options.learning_rate/(1.0+double(epoch)*0.005);
        for(std::size_t i=0;i<train_count;++i) {
            const auto& row=samples[order[i]];
            const double error=sigmoid(normalized_dot(model,row))-row.label;
            for(std::size_t j=0;j<dims;++j) {
                const double x=(row.features[j]-model.mean[j])/model.scale[j];
                const double gradient=std::max(-8.0,std::min(8.0,error*x+options.l2*model.weights[j]));
                model.weights[j]-=decay*gradient;
            }
            model.bias-=decay*error;
        }
        validate_model(model);
    }
    if(report) {
        TrainingReport info;
        info.samples=samples.size();
        info.train_samples=train_count;
        info.validation_samples=validation_count;
        std::size_t correct=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto& row=samples[order[i]];
            const double p=predict_probability(model,row.features);
            if(i<train_count)info.train_loss+=loss(p,row.label);
            else {
                info.validation_loss+=loss(p,row.label);
                if((p>=0.5)==(row.label==1.0))++correct;
            }
        }
        info.train_loss/=train_count;
        info.validation_loss/=validation_count;
        info.validation_accuracy=double(correct)/double(validation_count);
        *report=info;
    }
    return model;
}
double predict_probability(const Model& model,const std::vector<double>& features) {
    validate_model(model);
    require(features.size()==model.weights.size(),"prediction feature count does not match model");
    for(auto v:features)require(finite(v),"nonfinite prediction feature");
    Sample row; row.features=features;
    return sigmoid(normalized_dot(model,row));
}
void save_model(const Model& model,const std::string& filename) {
    validate_model(model);
    require(!filename.empty(),"missing model output filename");
    const auto destination=fs::path(filename);
    auto temporary=fs::path(filename+".pending");
    // Simple, portable human-readable model. Avoid a dependency on JSON/Protobuf.
    {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        require(bool(out),"cannot write model");
        out<<std::setprecision(17)<<"NQAI1 "<<model.weights.size()<<"\n";
        for(auto x:model.mean)out<<x<<" ";
        out<<"\n";
        for(auto x:model.scale)out<<x<<" ";
        out<<"\n";
        for(auto x:model.weights)out<<x<<" ";
        out<<"\n"<<model.bias<<"\n";
        out.flush();
        require(bool(out),"failed writing complete model");
    }
    std::error_code error;
    fs::rename(temporary,destination,error);
    if(error) {
        fs::remove(temporary);
        throw std::runtime_error("Noqeri AI: model rename failed: "+error.message());
    }
}
Model load_model(const std::string& filename) {
    std::ifstream in(filename,std::ios::binary);
    require(bool(in),"cannot open model");
    std::string tag;std::size_t dims=0;
    in>>tag>>dims;
    require(bool(in)&&tag=="NQAI1"&&dims>0&&dims<=max_features,"invalid NQAI1 model header");
    Model model;
    model.mean.resize(dims);model.scale.resize(dims);model.weights.resize(dims);
    for(auto& value:model.mean)in>>value;
    for(auto& value:model.scale)in>>value;
    for(auto& value:model.weights)in>>value;
    in>>model.bias;
    require(bool(in),"truncated model");
    std::string extra;
    require(!(in>>extra),"trailing model data");
    validate_model(model);
    return model;
}
AgentReport run_agent(const Model& model,const std::vector<Sample>& observations,
                      const AgentPolicy& policy,const ToolCallback& callback) {
    validate_model(model);
    require(policy.max_steps>0&&policy.max_steps<=10000,"agent max steps must be 1..10000");
    require(policy.max_tool_calls<=policy.max_steps,"invalid agent tool budget");
    AgentReport result;
    for(std::size_t i=0;i<observations.size();++i) {
        if(result.steps>=policy.max_steps) {
            result.budget_exhausted=true;
            break;
        }
        const double probability=predict_probability(model,observations[i].features);
        ++result.steps;
        ++result.observations;
        // A real local perception -> model decision -> authorized tool cycle.
        // Only a caller-provided callback can execute a tool: no ambient
        // process, filesystem, network, database or device capabilities.
        if(!policy.allow_emit || result.tool_calls>=policy.max_tool_calls) {
            ++result.denied_calls;
            continue;
        }
        const auto action=probability>=0.5?"emit_positive":"emit_negative";
        if(callback && callback(action,i,probability))++result.tool_calls;
        else ++result.denied_calls;
    }
    return result;
}
} // namespace noqeri_ai
