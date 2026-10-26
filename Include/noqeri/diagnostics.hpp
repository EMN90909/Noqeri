#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace noe {

using SourceId = std::uint32_t;

struct Span {
    std::size_t start=0;
    std::size_t end=0;
    std::size_t line=1;
    std::size_t column=1;
    SourceId source=0;
};

struct Diagnostic {
    std::string code;
    std::string message;
    Span span;
    std::string help;
};

class Diagnostics {
public:
    void error(std::string code,Span span,std::string message,std::string help={});
    bool hasErrors() const { return !items_.empty(); }
    const std::vector<Diagnostic>& items() const { return items_; }
    void registerSource(SourceId id,std::string name,std::string text={}) {
        sources_[id]=std::move(name);
        sourceTexts_[id]=std::move(text);
    }
    std::string sourceName(SourceId id,const std::string& fallback={}) const {
        auto it=sources_.find(id);
        return it==sources_.end()?fallback:it->second;
    }
    void print(const std::string& sourceName) const;
    // Versioned machine-readable diagnostic array for tools and AI agents.
    std::string toJson(const std::string& fallbackSourceName={}) const;
private:
    std::vector<Diagnostic> items_;
    std::unordered_map<SourceId,std::string> sources_;
    std::unordered_map<SourceId,std::string> sourceTexts_;
};

} // namespace noe
