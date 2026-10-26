#include "noe.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace noe {
namespace {
std::vector<std::unique_ptr<std::uint8_t[]>> allocations;
// Explicit ownership for typed strings stored in aggregate record fields.
// Do not dereference untrusted integer pointers as C strings.
std::unordered_map<std::uintptr_t,std::string> aggregateStrings;
std::mutex atomicMutex;
std::unordered_map<std::int64_t,std::vector<std::int64_t>> growingIntLists;
std::int64_t nextGrowingIntList=1;
constexpr std::size_t maxGrowingElements=1000000;

bool truthyValue(const NirValue&v){if(auto p=std::get_if<bool>(&v))return*p;if(auto p=std::get_if<std::int64_t>(&v))return*p!=0;if(auto p=std::get_if<double>(&v))return*p!=0.0;if(auto p=std::get_if<std::string>(&v))return!p->empty();return false;}
std::int64_t asInt(const NirValue&v){if(auto p=std::get_if<std::int64_t>(&v))return*p;if(auto p=std::get_if<bool>(&v))return*p?1:0;if(std::holds_alternative<std::monostate>(v))return 0;throw std::runtime_error("integer or pointer value required");}
double asDouble(const NirValue&v){if(auto p=std::get_if<double>(&v))return*p;if(auto p=std::get_if<std::int64_t>(&v))return static_cast<double>(*p);throw std::runtime_error("numeric value required");}

bool checkedOverflowEnabled(){
    static const bool enabled=[](){
        const char* value=std::getenv("NOQERI_CHECKED_OVERFLOW");
        if(!value)return false;
        const std::string text(value);
        return text=="1"||text=="true"||text=="on"||text=="yes";
    }();
    return enabled;
}

std::int64_t wrapped(std::uint64_t value){std::int64_t out=0;std::memcpy(&out,&value,sizeof(out));return out;}
// Values are stored in the generic signed 64-bit NIR variant, but narrow
// integer operations must wrap and sign/zero extend to their declared type.
// Work in unsigned bit-patterns; signed narrowing casts may be undefined.
std::int64_t typedBits(std::int64_t value,TypeKind kind){
    unsigned width=64;
    bool sign=false;
    switch(kind){
        case TypeKind::U8:width=8;break;
        case TypeKind::U16:width=16;break;
        case TypeKind::U32:width=32;break;
        case TypeKind::I8:width=8;sign=true;break;
        case TypeKind::I16:width=16;sign=true;break;
        case TypeKind::I32:width=32;sign=true;break;
        default:return value;
    }
    const std::uint64_t mask=(std::uint64_t{1}<<width)-1;
    std::uint64_t bits=static_cast<std::uint64_t>(value)&mask;
    if(sign&&(bits&(std::uint64_t{1}<<(width-1))))bits|=~mask;
    return wrapped(bits);
}
bool unsignedOperand(TypeKind k){
    return k==TypeKind::U8||k==TypeKind::U16||
           k==TypeKind::U32||k==TypeKind::U64||k==TypeKind::Usize;
}

std::int64_t addInt(std::int64_t a,std::int64_t b){
    if(checkedOverflowEnabled()){
        if((b>0&&a>std::numeric_limits<std::int64_t>::max()-b)||(b<0&&a<std::numeric_limits<std::int64_t>::min()-b))throw std::runtime_error("checked integer overflow in addition");
        return a+b;
    }
    return wrapped(static_cast<std::uint64_t>(a)+static_cast<std::uint64_t>(b));
}
std::int64_t subInt(std::int64_t a,std::int64_t b){
    if(checkedOverflowEnabled()){
        if((b<0&&a>std::numeric_limits<std::int64_t>::max()+b)||(b>0&&a<std::numeric_limits<std::int64_t>::min()+b))throw std::runtime_error("checked integer overflow in subtraction");
        return a-b;
    }
    return wrapped(static_cast<std::uint64_t>(a)-static_cast<std::uint64_t>(b));
}
bool mulOverflows(std::int64_t a,std::int64_t b){
    if(a==0||b==0)return false;
    const auto min=std::numeric_limits<std::int64_t>::min();
    const auto max=std::numeric_limits<std::int64_t>::max();
    if(a==-1)return b==min;
    if(b==-1)return a==min;
    if(a>0){if(b>0)return a>max/b;return b<min/a;}
    if(b>0)return a<min/b;
    return a<max/b;
}
std::int64_t mulInt(std::int64_t a,std::int64_t b){
    if(checkedOverflowEnabled()&&mulOverflows(a,b))throw std::runtime_error("checked integer overflow in multiplication");
    if(checkedOverflowEnabled())return a*b;
    return wrapped(static_cast<std::uint64_t>(a)*static_cast<std::uint64_t>(b));
}
std::int64_t divInt(std::int64_t a,std::int64_t b){
    if(b==0)throw std::runtime_error("integer division by zero");
    if(a==std::numeric_limits<std::int64_t>::min()&&b==-1){if(checkedOverflowEnabled())throw std::runtime_error("checked integer overflow in division");return a;}
    return a/b;
}
std::int64_t modInt(std::int64_t a,std::int64_t b){
    if(b==0)throw std::runtime_error("integer remainder by zero");
    if(a==std::numeric_limits<std::int64_t>::min()&&b==-1)return 0;
    return a%b;
}
std::int64_t negateInt(std::int64_t value){
    if(value==std::numeric_limits<std::int64_t>::min()){if(checkedOverflowEnabled())throw std::runtime_error("checked integer overflow in negation");return value;}
    return-value;
}

NirValue binary(const std::string&op,const NirValue&a,const NirValue&b){if(auto x=std::get_if<std::int64_t>(&a))if(auto y=std::get_if<std::int64_t>(&b)){if(op=="+")return addInt(*x,*y);if(op=="-")return subInt(*x,*y);if(op=="*")return mulInt(*x,*y);if(op=="/")return divInt(*x,*y);if(op=="%")return modInt(*x,*y);if(op=="==")return*x==*y;if(op=="!=")return*x!=*y;if(op=="<")return*x<*y;if(op=="<=")return*x<=*y;if(op==">")return*x>*y;if(op==">=")return*x>=*y;}if((std::holds_alternative<double>(a)||std::holds_alternative<std::int64_t>(a))&&(std::holds_alternative<double>(b)||std::holds_alternative<std::int64_t>(b))){double x=asDouble(a),y=asDouble(b);if(op=="+")return x+y;if(op=="-")return x-y;if(op=="*")return x*y;if(op=="/")return x/y;if(op=="==")return x==y;if(op=="!=")return x!=y;if(op=="<")return x<y;if(op=="<=")return x<=y;if(op==">")return x>y;if(op==">=")return x>=y;}if(auto x=std::get_if<std::string>(&a))if(auto y=std::get_if<std::string>(&b)){if(op=="+")return*x+*y;if(op=="==")return*x==*y;if(op=="!=")return*x!=*y;}if(auto x=std::get_if<bool>(&a))if(auto y=std::get_if<bool>(&b)){
    if(op=="==")return *x==*y;
    if(op=="!=")return *x!=*y;
}// A typed pointer-null comparison may reach the reference runtime with
// null represented by monostate and the pointer as a numeric ABI address.
if(op=="=="||op=="!="){
    const bool leftNull=std::holds_alternative<std::monostate>(a);
    const bool rightNull=std::holds_alternative<std::monostate>(b);
    if(leftNull && std::holds_alternative<std::int64_t>(b)) {
        const bool eq=std::get<std::int64_t>(b)==0;
        return op=="==" ? eq : !eq;
    }
    if(rightNull && std::holds_alternative<std::int64_t>(a)) {
        const bool eq=std::get<std::int64_t>(a)==0;
        return op=="==" ? eq : !eq;
    }
    // A boolean emitted as an ABI scalar must retain its truth value
    // when compared with a bool literal or other runtime bool.
    if(auto x=std::get_if<bool>(&a))if(auto y=std::get_if<std::int64_t>(&b)) {
        const bool eq=(*x==(*y!=0));
        return op=="==" ? eq : !eq;
    }
    if(auto x=std::get_if<std::int64_t>(&a))if(auto y=std::get_if<bool>(&b)) {
        const bool eq=((*x!=0)==*y);
        return op=="==" ? eq : !eq;
    }
}
if(std::holds_alternative<std::monostate>(a)&&std::holds_alternative<std::monostate>(b)){
    if(op=="==")return true;
    if(op=="!=")return false;
}if(op=="&&")return truthyValue(a)&&truthyValue(b);if(op=="||")return truthyValue(a)||truthyValue(b);throw std::runtime_error("unsupported binary operation "+op+" (left="+std::to_string(a.index())+", right="+std::to_string(b.index())+")");}
// NIR comparison results have Boolean type; signedness comes from the
// LEFT operand. The native backend follows that same rule, including
// unsigned 64-bit division/modulo for values above INT64_MAX.
NirValue typedBinary(const std::string& op,const NirValue& a,
                     const NirValue& b,TypeKind operand,TypeKind result){
    const auto x=std::get_if<std::int64_t>(&a);
    const auto y=std::get_if<std::int64_t>(&b);
    if(x&&y&&unsignedOperand(operand)){
        const auto lhs=static_cast<std::uint64_t>(*x);
        const auto rhs=static_cast<std::uint64_t>(*y);
        if(op=="<")return lhs<rhs;
        if(op=="<=")return lhs<=rhs;
        if(op==">")return lhs>rhs;
        if(op==">=")return lhs>=rhs;
        if(op=="/"||op=="%"){
            if(rhs==0)throw std::runtime_error(
                op=="/"?"integer division by zero":
                        "integer remainder by zero");
            return typedBits(wrapped(op=="/"?lhs/rhs:lhs%rhs),result);
        }
        if(op=="+")return typedBits(wrapped(lhs+rhs),result);
        if(op=="-")return typedBits(wrapped(lhs-rhs),result);
        if(op=="*")return typedBits(wrapped(lhs*rhs),result);
    }
    NirValue value=binary(op,a,b);
    if(x&&y&&(op=="+"||op=="-"||op=="*"||op=="/"||op=="%"))
        return typedBits(asInt(value),result);
    return value;
}
std::uint64_t rawLoad(std::uintptr_t address,std::size_t width,bool vol){if(address==0)throw std::runtime_error("null pointer dereference");switch(width){case 1:return vol?*reinterpret_cast<volatile std::uint8_t*>(address):*reinterpret_cast<std::uint8_t*>(address);case 2:return vol?*reinterpret_cast<volatile std::uint16_t*>(address):*reinterpret_cast<std::uint16_t*>(address);case 4:return vol?*reinterpret_cast<volatile std::uint32_t*>(address):*reinterpret_cast<std::uint32_t*>(address);case 8:return vol?*reinterpret_cast<volatile std::uint64_t*>(address):*reinterpret_cast<std::uint64_t*>(address);default:throw std::runtime_error("unsupported memory load width");}}
void rawStore(std::uintptr_t address,std::size_t width,std::uint64_t value,bool vol){if(address==0)throw std::runtime_error("null pointer store");switch(width){case 1:if(vol)*reinterpret_cast<volatile std::uint8_t*>(address)=static_cast<std::uint8_t>(value);else*reinterpret_cast<std::uint8_t*>(address)=static_cast<std::uint8_t>(value);break;case 2:if(vol)*reinterpret_cast<volatile std::uint16_t*>(address)=static_cast<std::uint16_t>(value);else*reinterpret_cast<std::uint16_t*>(address)=static_cast<std::uint16_t>(value);break;case 4:if(vol)*reinterpret_cast<volatile std::uint32_t*>(address)=static_cast<std::uint32_t>(value);else*reinterpret_cast<std::uint32_t*>(address)=static_cast<std::uint32_t>(value);break;case 8:if(vol)*reinterpret_cast<volatile std::uint64_t*>(address)=static_cast<std::uint64_t>(value);else*reinterpret_cast<std::uint64_t*>(address)=static_cast<std::uint64_t>(value);break;default:throw std::runtime_error("unsupported memory store width");}}
std::uintptr_t allocateBytes(std::size_t bytes){auto p=std::make_unique<std::uint8_t[]>(std::max<std::size_t>(bytes,1));std::memset(p.get(),0,std::max<std::size_t>(bytes,1));auto address=reinterpret_cast<std::uintptr_t>(p.get());allocations.push_back(std::move(p));return address;}
}

std::string Interpreter::valueToString(const NirValue&v)const{if(std::holds_alternative<std::monostate>(v))return"null";if(auto p=std::get_if<std::int64_t>(&v))return std::to_string(*p);if(auto p=std::get_if<double>(&v))return std::to_string(*p);if(auto p=std::get_if<bool>(&v))return*p?"true":"false";return std::get<std::string>(v);}
int Interpreter::run(const NirProgram&p){try{
    if(!p.entry.partitions.empty())throw std::runtime_error("PGAS partitions require a distributed backend; local interpreter is disabled");
    for(const auto&fn:p.functions)if(!fn.partitions.empty())throw std::runtime_error("PGAS partitions require a distributed backend; local interpreter is disabled");
    allocations.clear();aggregateStrings.clear();growingIntLists.clear();runFunction(p,p.entry,{});aggregateStrings.clear();allocations.clear();growingIntLists.clear();return 0;}catch(const std::exception&e){aggregateStrings.clear();allocations.clear();growingIntLists.clear();std::cerr<<"NQR-R4000: runtime error: "<<e.what()<<"\n";return 1;}}
NirValue Interpreter::runFunction(const NirProgram&p,const NirFunction&fn,const std::vector<NirValue>&argv){
    if(fn.isExtern)throw std::runtime_error("cannot directly interpret extern function "+fn.name);
    std::unordered_map<std::string,NirValue>vars;std::unordered_map<std::string,std::uint64_t>cells;std::unordered_set<std::string>addressTaken;std::vector<NirValue>regs(fn.nextReg);
    for(std::size_t i=0;i<fn.params.size()&&i<argv.size();++i){vars[fn.params[i]]=argv[i];if(std::holds_alternative<std::int64_t>(argv[i]))cells[fn.params[i]]=static_cast<std::uint64_t>(std::get<std::int64_t>(argv[i]));}
    std::size_t pc=0;
    while(pc<fn.code.size()){
        const auto&i=fn.code[pc];auto get=[&](Reg r)->const NirValue&{if(r>=regs.size())throw std::runtime_error("invalid NIR register");return regs[r];};
        switch(i.op){
            case NirOp::Const:regs[*i.dest]=i.literal;break;
            case NirOp::Load:{if(addressTaken.count(i.text)){regs[*i.dest]=static_cast<std::int64_t>(cells[i.text]);break;}auto it=vars.find(i.text);if(it==vars.end())throw std::runtime_error("undefined variable "+i.text);regs[*i.dest]=it->second;break;}
            case NirOp::Store:{vars[i.text]=get(i.args[0]);if(auto n=std::get_if<std::int64_t>(&vars[i.text]))cells[i.text]=static_cast<std::uint64_t>(*n);else if(auto b=std::get_if<bool>(&vars[i.text]))cells[i.text]=*b?1:0;break;}
            case NirOp::AddressOf:addressTaken.insert(i.text);cells.try_emplace(i.text,0);regs[*i.dest]=static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&cells[i.text]));break;
            case NirOp::LoadMemory:{
                const auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));
                const auto bits=rawLoad(address,i.width,i.isVolatile);
                if(i.type.kind==TypeKind::String){
                    if(i.width!=sizeof(std::uintptr_t))throw std::runtime_error("unsupported string memory width");
                    if(bits==0)regs[*i.dest]=std::string{};
                    else {
                        const auto owned=aggregateStrings.find(static_cast<std::uintptr_t>(bits));
                        if(owned==aggregateStrings.end())throw std::runtime_error("string field contains an unmanaged pointer");
                        regs[*i.dest]=owned->second;
                    }
                }else if(i.type.kind==TypeKind::Float){
                    if(i.width!=sizeof(double))throw std::runtime_error("unsupported float memory width");
                    double value=0.0;
                    std::memcpy(&value,&bits,sizeof(value));
                    regs[*i.dest]=value;
                }else if(i.type.kind==TypeKind::Bool){
                    regs[*i.dest]=(bits!=0);
                }else regs[*i.dest]=typedBits(wrapped(bits),i.type.kind);
                break;
            }
            case NirOp::StoreMemory:{
                const auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));
                const NirValue& source=get(i.args[1]);
                std::uint64_t bits=0;
                if(auto text=std::get_if<std::string>(&source)){
                    if(i.width!=sizeof(std::uintptr_t))throw std::runtime_error("unsupported string memory width");
                    if(text->size()>1024*1024)throw std::runtime_error("string field exceeds the 1 MiB interpreter limit");
                    const auto owned=allocateBytes(text->size()+1);
                    std::memcpy(reinterpret_cast<void*>(owned),text->data(),text->size());
                    aggregateStrings.emplace(owned,*text);
                    bits=static_cast<std::uint64_t>(owned);
                }else if(auto value=std::get_if<double>(&source)){
                    if(i.width!=sizeof(double))throw std::runtime_error("unsupported float memory width");
                    std::memcpy(&bits,value,sizeof(bits));
                }else bits=static_cast<std::uint64_t>(asInt(source));
                rawStore(address,i.width,bits,i.isVolatile);
                break;
            }
            case NirOp::PtrOffset:{std::int64_t base=asInt(get(i.args[0]));std::int64_t delta=static_cast<std::int64_t>(i.target);if(i.args.size()>1)delta=mulInt(asInt(get(i.args[1])),static_cast<std::int64_t>(i.width));regs[*i.dest]=addInt(base,delta);break;}
            case NirOp::StackAlloc:regs[*i.dest]=static_cast<std::int64_t>(allocateBytes(i.width));break;
            case NirOp::MakeSlice:{auto descriptor=allocateBytes(16);rawStore(descriptor,8,static_cast<std::uint64_t>(asInt(get(i.args[0]))),false);rawStore(descriptor+8,8,static_cast<std::uint64_t>(asInt(get(i.args[1]))),false);regs[*i.dest]=static_cast<std::int64_t>(descriptor);break;}
            case NirOp::SliceData:{auto descriptor=static_cast<std::uintptr_t>(asInt(get(i.args[0])));regs[*i.dest]=static_cast<std::int64_t>(rawLoad(descriptor,8,false));break;}
            case NirOp::SliceLen:{auto descriptor=static_cast<std::uintptr_t>(asInt(get(i.args[0])));regs[*i.dest]=static_cast<std::int64_t>(rawLoad(descriptor+8,8,false));break;}
            case NirOp::CheckNonNull:{if(i.args.empty()||asInt(get(i.args[0]))==0)throw std::runtime_error("null pointer or slice access");break;}
            case NirOp::CheckBounds:{if(i.args.size()<2)throw std::runtime_error("malformed bounds check");const auto index=asInt(get(i.args[0]));const auto length=asInt(get(i.args[1]));if(index<0||length<0||index>=length)throw std::runtime_error("index out of bounds: index="+std::to_string(index)+", length="+std::to_string(length));break;}
            case NirOp::AtomicLoad:{std::lock_guard<std::mutex> lock(atomicMutex);auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));regs[*i.dest]=static_cast<std::int64_t>(rawLoad(address,i.width,true));break;}
            case NirOp::AtomicStore:{std::lock_guard<std::mutex> lock(atomicMutex);auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));rawStore(address,i.width,static_cast<std::uint64_t>(asInt(get(i.args[1]))),true);break;}
            case NirOp::AtomicExchange:{std::lock_guard<std::mutex> lock(atomicMutex);auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));auto old=rawLoad(address,i.width,true);rawStore(address,i.width,static_cast<std::uint64_t>(asInt(get(i.args[1]))),true);regs[*i.dest]=static_cast<std::int64_t>(old);break;}
            case NirOp::AtomicCompareExchange:{std::lock_guard<std::mutex> lock(atomicMutex);auto address=static_cast<std::uintptr_t>(asInt(get(i.args[0])));auto old=rawLoad(address,i.width,true);auto expected=static_cast<std::uint64_t>(asInt(get(i.args[1])));if(old==expected)rawStore(address,i.width,static_cast<std::uint64_t>(asInt(get(i.args[2]))),true);regs[*i.dest]=static_cast<std::int64_t>(old);break;}
            case NirOp::AtomicFence:std::atomic_thread_fence(std::memory_order_seq_cst);if(i.dest)regs[*i.dest]=std::monostate{};break;
            case NirOp::Intrinsic:{if(i.text=="x86.rdtsc"){auto now=std::chrono::high_resolution_clock::now().time_since_epoch().count();regs[*i.dest]=static_cast<std::int64_t>(now);}else if(i.text=="compiler.fence")std::atomic_signal_fence(std::memory_order_seq_cst);else if(i.text=="x86.pause"){}else if(i.text=="x86.halt")throw std::runtime_error("x86.halt is not executable in the reference interpreter");else throw std::runtime_error("unknown intrinsic "+i.text);if(i.dest&&i.text!="x86.rdtsc")regs[*i.dest]=std::monostate{};break;}
            case NirOp::InlineAsm:if(i.dest)regs[*i.dest]=std::monostate{};break;
            case NirOp::Try:{auto value=asInt(get(i.args[0]));if(value<0)return NirValue(value);regs[*i.dest]=value;break;}
            case NirOp::Throw:{auto code=asInt(get(i.args[0]));return NirValue(code<0?code:-(code+1));}
            case NirOp::Cast:{auto v=get(i.args[0]);Type t=typeFromName(i.text);if(t.kind==TypeKind::Float)regs[*i.dest]=asDouble(v);else if(t.isInteger())regs[*i.dest]=typedBits(asInt(v),t.kind);else if(t.kind==TypeKind::Pointer)regs[*i.dest]=asInt(v);else regs[*i.dest]=v;break;}
            case NirOp::Unary:{auto v=get(i.args[0]);if(i.text=="!")regs[*i.dest]=!truthyValue(v);else if(i.text=="-"){if(auto n=std::get_if<std::int64_t>(&v))regs[*i.dest]=typedBits(negateInt(*n),i.type.kind);else regs[*i.dest]=-asDouble(v);}else regs[*i.dest]=v;break;}
            case NirOp::Binary:{
                const auto kind=i.args[0]<fn.registerTypes.size()?
                    fn.registerTypes[i.args[0]].kind:TypeKind::Unknown;
                regs[*i.dest]=typedBinary(i.text,get(i.args[0]),get(i.args[1]),
                                         kind,i.type.kind);
                break;
            }
            case NirOp::Call:{std::vector<NirValue>a;for(auto r:i.args)a.push_back(get(r));auto user=std::find_if(p.functions.begin(),p.functions.end(),[&](const auto&x){return x.name==i.text&&!x.isExtern;});if(user!=p.functions.end()){regs[*i.dest]=runFunction(p,*user,a);break;}if(i.text=="nq_list_new"){
                    if(nextGrowingIntList==std::numeric_limits<std::int64_t>::max())
                        throw std::runtime_error("growable list ID space exhausted");
                    const auto id=nextGrowingIntList++;
                    growingIntLists.emplace(id,std::vector<std::int64_t>{});
                    regs[*i.dest]=id;break;
                }
                if(i.text=="nq_list_append"||i.text=="nq_list_length"||i.text=="nq_list_close"||i.text=="nq_list_at"){
                    if(a.empty())throw std::runtime_error("growable list handle missing");
                    const auto id=asInt(a[0]);
                    auto it=growingIntLists.find(id);
                    if(it==growingIntLists.end())throw std::runtime_error("growable list has been released or is invalid");
                    if(i.text=="nq_list_at"){
                        if(a.size()!=2)throw std::runtime_error("growable list index requires one integer");
                        const auto at=asInt(a[1]);
                        if(at<0 || static_cast<std::uint64_t>(at)>=it->second.size())
                            throw std::runtime_error("growable list index out of bounds");
                        regs[*i.dest]=it->second[static_cast<std::size_t>(at)];
                    }else if(i.text=="nq_list_append"){
                        if(a.size()!=2)throw std::runtime_error("growable list append requires one value");
                        if(it->second.size()>=maxGrowingElements)
                            throw std::runtime_error("growable list reached the one-million-element cap");
                        it->second.push_back(asInt(a[1]));regs[*i.dest]=std::int64_t{0};
                    }else if(i.text=="nq_list_length"){
                        regs[*i.dest]=static_cast<std::int64_t>(it->second.size());
                    }else{
                        growingIntLists.erase(it);regs[*i.dest]=std::int64_t{0};
                    }
                    break;
                }
                std::string error;if(i.text=="host"||i.text=="abi"){if(a.empty()||!std::holds_alternative<std::string>(a[0]))throw std::runtime_error(i.text+" requires a string service name");std::string service=std::get<std::string>(a[0]);std::vector<NirValue>serviceArgs(a.begin()+1,a.end());std::optional<NirValue>result;if(host_)result=callNoqeriAbi(host_,service,serviceArgs,error);if(!result)result=callNoqeriAbi(defaultNoqeriAbi(),service,serviceArgs,error);if(!result)throw std::runtime_error(error.empty()?"unknown ABI service "+service:error);regs[*i.dest]=*result;break;}std::optional<NirValue>result;if(host_)result=callNoqeriAbi(host_,i.text,a,error);if(!result)result=callNoqeriAbi(defaultNoqeriAbi(),i.text,a,error);if(!result)throw std::runtime_error(error.empty()?"unknown function "+i.text:error);regs[*i.dest]=*result;break;}
            case NirOp::Jump:pc=i.target;continue;
            case NirOp::JumpIfFalse:if(!truthyValue(get(i.args[0]))){pc=i.target;continue;}break;
            case NirOp::Return:return i.args.empty()?NirValue(std::monostate{}):get(i.args[0]);
            case NirOp::Nop:break;
        }
        ++pc;
    }
    return std::monostate{};
}
} // namespace noe
