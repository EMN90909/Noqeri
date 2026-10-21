static PointerPtr pointerAdd(const PointerPtr& base, std::int64_t index, std::size_t width, std::int64_t immediate) {
    if (!base) throw RuntimeError("pointer arithmetic on null pointer");
    const auto actualWidth = std::max<std::size_t>(1, width);
    if (actualWidth > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) throw RuntimeError("pointer width exceeds host range");
    const auto delta = checkedAdd(checkedMul(index, static_cast<std::int64_t>(actualWidth)), immediate);
    auto out = std::make_shared<Pointer>(*base);
    out->offset = checkedAdd(out->offset, delta); return out;
}

static std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o > b.size() || b.size() - o < 4) throw RuntimeError("truncated NQRI u32");
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o+1])<<8) | (static_cast<std::uint32_t>(b[o+2])<<16) | (static_cast<std::uint32_t>(b[o+3])<<24);
}
static std::uint64_t u64(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o > b.size() || b.size() - o < 8) throw RuntimeError("truncated NQRI u64");
    std::uint64_t v=0; for(std::size_t i=0;i<8;++i)v|=static_cast<std::uint64_t>(b[o+i])<<(i*8); return v;
}
static std::int64_t i64(const std::vector<std::uint8_t>& b, std::size_t o) { auto v=u64(b,o); std::int64_t out; std::memcpy(&out,&v,sizeof(out)); return out; }
static double f64bits(std::uint64_t bits) { double out; std::memcpy(&out,&bits,sizeof(out)); return out; }

struct Function {
    std::uint64_t symbol=0; std::uint32_t nameStart=0,nameEnd=0,firstInstruction=0,instructionCount=0,parameterCount=0,flags=0; std::string name;
};
struct Instruction {
    std::uint32_t opcode=0,typeCode=0,flags=0; std::int64_t dest=-1,left=-1,right=-1,immediate=0; std::uint64_t symbol=0,width=0,sourceStart=0,sourceEnd=0; double floatImmediate=0;
};
struct Module {
    std::vector<std::uint8_t> bytes, source; std::string sourceText; std::vector<Function> functions; std::vector<Instruction> instructions;
    std::unordered_map<std::uint64_t,std::size_t> bySymbol; std::unordered_map<std::string,std::size_t> byName; std::unordered_set<std::string> recordTypes;
};
static std::string spanText(const Module& module, const Instruction& ins) {
    if (ins.sourceEnd <= ins.sourceStart || ins.sourceEnd > module.source.size()) return {};
    return std::string(reinterpret_cast<const char*>(module.source.data()+ins.sourceStart), static_cast<std::size_t>(ins.sourceEnd-ins.sourceStart));
}
static Module loadNqri2(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size()<32 || bytes[0]!='N'||bytes[1]!='Q'||bytes[2]!='R'||bytes[3]!='I') throw RuntimeError("not an NQRI module");
    const auto version=u32(bytes,4); if(version!=2)throw RuntimeError("unsupported NQRI version "+std::to_string(version));
    const auto sourceBytes=u32(bytes,8), functionCount=u32(bytes,12), instructionCount=u32(bytes,16), functionBytes=u32(bytes,20), instructionBytes=u32(bytes,24);
    if(functionBytes!=40||instructionBytes!=96)throw RuntimeError("unsupported NQRI record layout");
    const std::uint64_t required=32ull+sourceBytes+static_cast<std::uint64_t>(functionCount)*40ull+static_cast<std::uint64_t>(instructionCount)*96ull;
    if(required!=bytes.size())throw RuntimeError("invalid NQRI length");
    Module m; m.bytes=bytes; m.source.assign(bytes.begin()+32,bytes.begin()+32+sourceBytes); m.sourceText.assign(reinterpret_cast<const char*>(m.source.data()),m.source.size());
    std::size_t off=32+sourceBytes;
    for(std::uint32_t n=0;n<functionCount;++n,off+=40){Function f;f.symbol=u64(bytes,off);f.nameStart=u32(bytes,off+8);f.nameEnd=u32(bytes,off+12);f.firstInstruction=u32(bytes,off+16);f.instructionCount=u32(bytes,off+20);f.parameterCount=u32(bytes,off+24);f.flags=u32(bytes,off+28);if(f.symbol==0||f.nameEnd<=f.nameStart||f.nameEnd>m.source.size()||static_cast<std::uint64_t>(f.firstInstruction)+f.instructionCount>instructionCount)throw RuntimeError("invalid function record");f.name.assign(reinterpret_cast<const char*>(m.source.data()+f.nameStart),f.nameEnd-f.nameStart);m.bySymbol[f.symbol]=m.functions.size();m.byName[f.name]=m.functions.size();m.functions.push_back(std::move(f));}
    for(std::uint32_t n=0;n<instructionCount;++n,off+=96){Instruction x;x.opcode=u32(bytes,off);x.typeCode=u32(bytes,off+4);x.flags=u32(bytes,off+8);x.dest=i64(bytes,off+16);x.left=i64(bytes,off+24);x.right=i64(bytes,off+32);x.immediate=i64(bytes,off+40);x.symbol=u64(bytes,off+48);x.width=u64(bytes,off+56);x.sourceStart=u64(bytes,off+64);x.sourceEnd=u64(bytes,off+72);x.floatImmediate=f64bits(u64(bytes,off+80));if(x.opcode>47||x.sourceEnd<x.sourceStart||x.sourceEnd>m.source.size())throw RuntimeError("invalid instruction record");m.instructions.push_back(x);}
    const std::regex rec(R"(\brecord\s+([A-Za-z_][A-Za-z0-9_]*))"); for(std::sregex_iterator it(m.sourceText.begin(),m.sourceText.end(),rec),end;it!=end;++it)m.recordTypes.insert((*it)[1].str());
    return m;
}

static std::string trim(std::string value) {
    auto first = std::find_if(value.begin(), value.end(), [](unsigned char c){ return !std::isspace(c); });
    auto last = std::find_if(value.rbegin(), value.rend(), [](unsigned char c){ return !std::isspace(c); }).base();
    if (first >= last) return {};
    return std::string(first, last);
}
static bool redundantForwardPrototype(const std::string& clean) {
    if (clean.rfind("function ", 0) != 0) return false;
    if (clean.find('{') != std::string::npos) return false;
    return clean.find('(') != std::string::npos && clean.find(')') != std::string::npos;
}
static std::vector<std::uint8_t> bundleManifest(const std::string& rootPath, const std::string& manifestPath) {
    namespace fs = std::filesystem;
    const fs::path root = fs::weakly_canonical(fs::path(rootPath));
    std::ifstream manifest(manifestPath);
    if (!manifest) throw RuntimeError("cannot open source manifest " + manifestPath);
    std::ostringstream merged;
    merged << "module noqeri.compiler.selfhost.bundle\n";
    std::unordered_set<std::string> seen;
    std::string relative;
    std::size_t sourceCount = 0;
    while (std::getline(manifest, relative)) {
        relative = trim(relative);
        if (relative.empty() || relative[0] == '#') continue;
        const fs::path listed(relative);
        if (listed.is_absolute() || listed.has_root_name() || listed.has_root_directory())
            throw RuntimeError("source manifest requires repository-relative paths: " + relative);
        if (listed.extension() != ".noqeri")
            throw RuntimeError("source manifest accepts only .noqeri files: " + relative);
        const fs::path candidate = fs::weakly_canonical(root / listed);
        const auto rel = candidate.lexically_relative(root);
        if (rel.empty() || *rel.begin() == ".." || rel == ".")
            throw RuntimeError("source manifest escapes repository root: " + relative);
        std::error_code sourceError;
        if (!fs::is_regular_file(candidate, sourceError) || sourceError)
            throw RuntimeError("source manifest entry is not a regular file: " + relative);
        // Compare canonical names so aliases and symlinks cannot include one file twice.
        if (!seen.insert(candidate.generic_string()).second)
            throw RuntimeError("duplicate source in manifest: " + relative);
        ++sourceCount;
        std::ifstream input(candidate, std::ios::binary);
        if (!input) throw RuntimeError("cannot open compiler source " + candidate.string());
        std::string line;
        while (std::getline(input, line)) {
            const auto clean = trim(line);
            if (clean.rfind("module ", 0) == 0 || clean.rfind("import ", 0) == 0) continue;
            // Module-local forward declarations are needed while parsing a single
            // source file, but become duplicate top-level declarations after the
            // deterministic compiler closure is flattened. Full definitions are
            // already present later in the same source closure.
            if (redundantForwardPrototype(clean)) continue;
            merged << line << '\n';
        }
        merged << '\n';
    }
    if (manifest.bad()) throw RuntimeError("failed reading source manifest " + manifestPath);
    if (sourceCount == 0) throw RuntimeError("source manifest contains no .noqeri files: " + manifestPath);
    const auto text = merged.str();
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

static std::vector<std::uint8_t> readFile(const std::string& path) { std::ifstream in(path,std::ios::binary); if(!in)throw RuntimeError("cannot open "+path); return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),{}); }
static void writeFile(const std::string& path,const std::vector<std::uint8_t>& bytes,std::size_t count){if(count>bytes.size())throw RuntimeError("write size exceeds buffer");std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw RuntimeError("cannot write "+path);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(count));if(!out)throw RuntimeError("failed writing "+path);}
static std::vector<std::uint8_t> decodeStringLiteral(const std::vector<std::uint8_t>& source,std::size_t start,std::size_t end){if(end<=start||end>source.size())throw RuntimeError("invalid string source span");std::string s(reinterpret_cast<const char*>(source.data()+start),end-start);if(s.size()>=2&&s.front()=='"'&&s.back()=='"')s=s.substr(1,s.size()-2);std::vector<std::uint8_t> out;for(std::size_t i=0;i<s.size();++i){char c=s[i];if(c=='\\'&&i+1<s.size()){char n=s[++i];switch(n){case'n':c='\n';break;case'r':c='\r';break;case't':c='\t';break;case'"':c='"';break;case'\\':c='\\';break;default:out.push_back('\\');c=n;break;}}out.push_back(static_cast<std::uint8_t>(c));}return out;}

class Vm {
public:
    explicit Vm(Module module,std::uint64_t maxSteps=50000000,std::size_t maxDepth=4096):module_(std::move(module)),maxSteps_(maxSteps),maxDepth_(maxDepth){}
    Value invoke(const std::string& name,const std::vector<Value>& args={}){steps_=0;auto it=module_.byName.find(name);if(it==module_.byName.end())throw RuntimeError("function not found: "+name);return execute(module_.functions[it->second],args,0);}
    SlicePtr bytesSlice(const std::vector<std::uint8_t>& bytes){auto block=std::make_shared<MemoryBlock>(bytes.size(),bytes.size());block->buffer=bytes;return sliceValue(blockPtr(block),bytes.size());}
    SlicePtr writableBytes(std::size_t bytes){auto block=std::make_shared<MemoryBlock>(bytes,bytes);return sliceValue(blockPtr(block),bytes);}
