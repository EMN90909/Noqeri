#pragma once
#include "nir.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace noe {
// Interpreter-collected, revision-bound NIR instruction counters. Profiles
// influence cost/size heuristics only; no profile may relax optimizer legality.
// Native machine-code instrumentation is a separate release requirement.
class NirExecutionProfile {
public:
    static std::string key(const NirFunction& fn) {
        return fn.name.empty() ? "@entry" : fn.name;
    }
    static std::string fingerprint(const NirProgram& program) {
        std::uint64_t digest=14695981039346656037ull;
        const auto byte=[&](std::uint8_t x) {
            digest^=x;
            digest*=1099511628211ull;
        };
        const auto integer=[&](std::uint64_t n) {
            for(unsigned shift=0;shift<64;shift+=8)byte(static_cast<std::uint8_t>(n>>shift));
        };
        const auto str=[&](const std::string& value){
            integer(value.size());
            for(unsigned char c:value)byte(c);
        };
        const auto fn=[&](const NirFunction& body) {
            str(key(body));integer(body.code.size());
            for(const auto& op:body.code) {
                integer(static_cast<std::uint64_t>(op.op));
                integer(op.dest?static_cast<std::uint64_t>(*op.dest)+1:0);
                integer(static_cast<std::uint64_t>(op.type.kind));
                integer(op.target);integer(op.width);integer(op.isVolatile?1:0);
                str(op.text);integer(op.args.size());
                for(auto arg:op.args)integer(arg);
                integer(op.literal.index());
                if(const auto v=std::get_if<std::int64_t>(&op.literal))
                    integer(static_cast<std::uint64_t>(*v));
                else if(const auto v=std::get_if<bool>(&op.literal))
                    integer(*v?1:0);
                else if(const auto v=std::get_if<std::string>(&op.literal))
                    str(*v);
                else if(const auto v=std::get_if<double>(&op.literal)) {
                    // Debug-only representation; the profile never authorizes
                    // floating point reordering or changes to results.
                    std::ostringstream stream;stream<<std::hexfloat<<*v;str(stream.str());
                }
            }
        };
        fn(program.entry);
        integer(program.functions.size());
        for(const auto& function:program.functions)fn(function);
        std::ostringstream hex;hex<<std::hex<<std::setw(16)<<std::setfill('0')<<digest;
        return hex.str();
    }
    void begin(const NirProgram& program) {
        source_=fingerprint(program);counters_.clear();
        counters_[key(program.entry)]=std::vector<std::uint64_t>(program.entry.code.size(),0);
        for(const auto& f:program.functions)
            counters_[key(f)]=std::vector<std::uint64_t>(f.code.size(),0);
    }
    bool compatible(const NirProgram& program) const {
        if(source_.empty()||source_!=fingerprint(program))return false;
        if(counters_.size()!=program.functions.size()+1)return false;
        auto same=[&](const NirFunction& fn) {
            auto it=counters_.find(key(fn));
            return it!=counters_.end()&&it->second.size()==fn.code.size();
        };
        if(!same(program.entry))return false;
        for(const auto& fn:program.functions)if(!same(fn))return false;
        return true;
    }
    void record(const std::string& function,std::size_t pc) {
        auto it=counters_.find(function);
        if(it==counters_.end()||pc>=it->second.size())return;
        auto& counter=it->second[pc];
        if(counter!=std::numeric_limits<std::uint64_t>::max())++counter;
    }
    std::uint64_t executions(const NirFunction& fn) const {
        const auto it=counters_.find(key(fn));
        if(it==counters_.end()||it->second.empty())return 0;
        // Maximum instruction count is a robust hotness approximation even
        // if loops execute only a subset of the function's instructions.
        return *std::max_element(it->second.begin(),it->second.end());
    }
    std::string serialize() const {
        std::ostringstream out;out<<"NQPGO1 "<<source_<<"\n";
        std::vector<std::string> names;
        for(const auto& pair:counters_)names.push_back(pair.first);
        std::sort(names.begin(),names.end());
        for(const auto& name:names) {
            const auto& counters=counters_.at(name);
            for(std::size_t pc=0;pc<counters.size();++pc)
                if(counters[pc])out<<name<<" "<<pc<<" "<<counters[pc]<<"\n";
        }
        return out.str();
    }
    static std::optional<NirExecutionProfile> parse(
        const std::string& data,const NirProgram& program) {
        if(data.size()>4*1024*1024)return std::nullopt;
        std::istringstream input(data);
        std::string magic,source;
        if(!(input>>magic>>source)||magic!="NQPGO1"||
           source!=fingerprint(program))return std::nullopt;
        NirExecutionProfile profile;profile.begin(program);
        std::string function;std::size_t pc=0;std::uint64_t hits=0;
        std::unordered_map<std::string,std::vector<bool>> visited;
        for(const auto& pair:profile.counters_)
            visited[pair.first]=std::vector<bool>(pair.second.size(),false);
        while(input>>function) {
            if(!(input>>pc>>hits)||hits==0)return std::nullopt;
            auto i=profile.counters_.find(function);
            if(i==profile.counters_.end()||pc>=i->second.size()||
               visited[function][pc])return std::nullopt;
            visited[function][pc]=true;
            i->second[pc]=hits;
        }
        if(!input.eof())return std::nullopt;
        return profile;
    }
private:
    std::string source_;
    std::unordered_map<std::string,std::vector<std::uint64_t>> counters_;
};
} // namespace noe
