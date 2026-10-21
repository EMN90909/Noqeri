    Value execute(const Function& fn,const std::vector<Value>& args,std::size_t depth){if(depth>maxDepth_)throw RuntimeError("NQRI call-depth limit exceeded");Frame frame;frame.args=args;frame.pc=fn.firstInstruction;const auto end=static_cast<std::size_t>(fn.firstInstruction)+fn.instructionCount;try{while(frame.pc<end){if(++steps_>maxSteps_)throw RuntimeError("NQRI instruction-step limit exceeded: "+std::to_string(maxSteps_)+" function="+fn.name+" pc="+std::to_string(frame.pc));const auto& ins=module_.instructions.at(frame.pc);const auto next=frame.pc+1;Value value;switch(ins.opcode){case 0:case 44:break;case 1:setReg(frame,ins.dest,ins.immediate);break;case 2:setReg(frame,ins.dest,ins.floatImmediate);break;case 3:setReg(frame,ins.dest,getReg(frame,ins.left,ins));break;case 4:setReg(frame,ins.dest,cell(frame,ins.symbol)->value);break;case 5:cell(frame,ins.symbol)->value=getReg(frame,ins.left,ins);break;case 6:case 7:case 8:case 9:case 10:{const auto&a=getReg(frame,ins.left,ins);const auto&b=getReg(frame,ins.right,ins);if(std::holds_alternative<double>(a)||std::holds_alternative<double>(b)){double x=asDouble(a),y=asDouble(b);if((ins.opcode==9||ins.opcode==10)&&y==0.0)throw RuntimeError("division by zero");double r=ins.opcode==6?x+y:ins.opcode==7?x-y:ins.opcode==8?x*y:ins.opcode==9?x/y:std::fmod(x,y);setReg(frame,ins.dest,r);}else{auto x=asInt(a),y=asInt(b);if((ins.opcode==9||ins.opcode==10)&&y==0)throw RuntimeError("division by zero");std::int64_t r=0;if(ins.flags&2){if(ins.opcode==6)r=checkedAdd(x,y);else if(ins.opcode==7)r=checkedSub(x,y);else if(ins.opcode==8)r=checkedMul(x,y);else if(ins.opcode==9){if(x==std::numeric_limits<std::int64_t>::min()&&y==-1)throw RuntimeError("checked i64 overflow");r=x/y;}else r=(x==std::numeric_limits<std::int64_t>::min()&&y==-1)?0:x%y;}else{if(ins.opcode==6)r=wrapAdd(x,y);else if(ins.opcode==7)r=wrapSub(x,y);else if(ins.opcode==8)r=wrapMul(x,y);else if(ins.opcode==9)r=(x==std::numeric_limits<std::int64_t>::min()&&y==-1)?x:x/y;else r=(x==std::numeric_limits<std::int64_t>::min()&&y==-1)?0:x%y;}setReg(frame,ins.dest,r);}break;}case 11:{const auto&a=getReg(frame,ins.left,ins);if(std::holds_alternative<double>(a))setReg(frame,ins.dest,-asDouble(a));else{auto x=asInt(a);if(ins.flags&2)setReg(frame,ins.dest,checkedSub(0,x));else setReg(frame,ins.dest,wrapSub(0,x));}break;}case 12:setReg(frame,ins.dest,truthy(getReg(frame,ins.left,ins))?std::int64_t{0}:std::int64_t{1});break;case 13:case 14:case 15:case 16:case 17:case 18:{const auto&a=getReg(frame,ins.left,ins);const auto&b=getReg(frame,ins.right,ins);bool r=false;if(ins.opcode==13||ins.opcode==14){r=valueEqual(a,b);if(ins.opcode==14)r=!r;}else{const int c=compare(a,b);r=ins.opcode==15?c<0:ins.opcode==16?c<=0:ins.opcode==17?c>0:c>=0;}setReg(frame,ins.dest,r?std::int64_t{1}:std::int64_t{0});break;}case 19:{auto target=checkedSize(ins.immediate,"jump target");if(target<fn.firstInstruction||target>=end)throw RuntimeError("jump target outside function");frame.pc=target;continue;}case 20:if(!truthy(getReg(frame,ins.left,ins))){auto target=checkedSize(ins.immediate,"branch target");if(target<fn.firstInstruction||target>=end)throw RuntimeError("branch target outside function");frame.pc=target;continue;}break;case 21:{std::vector<Value> callArgs;for(std::size_t i=0;i<ins.width;++i){auto it=frame.pendingArgs.find(i);callArgs.push_back(it==frame.pendingArgs.end()?Value{std::int64_t{0}}:it->second);}frame.pendingArgs.clear();auto target=module_.bySymbol.find(ins.symbol);value=target!=module_.bySymbol.end()?execute(module_.functions[target->second],callArgs,depth+1):hostCall(spanText(module_,ins),callArgs,ins,depth);setReg(frame,ins.dest,value);break;}case 22:return ins.left>=0?getReg(frame,ins.left,ins):Value{std::int64_t{0}};case 23:throw RuntimeError("phi requires CFG predecessor metadata and is not emitted by current Stage-2 lowering");case 24:{auto c=cell(frame,ins.symbol);if(auto p=std::get_if<PointerPtr>(&c->value);p&&*p)setReg(frame,ins.dest,*p);else setReg(frame,ins.dest,cellPtr(c));break;}case 25:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("ptr_add base is not a pointer");setReg(frame,ins.dest,pointerAdd(*p,asInt(getReg(frame,ins.right,ins)),ins.width?static_cast<std::size_t>(ins.width):1,ins.immediate));break;}case 26:case 28:case 29:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("atomic access requires pointer");setReg(frame,ins.dest,readScalar(*p,ins.width?static_cast<std::size_t>(ins.width):8,ins.typeCode));break;}case 27:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("atomic store requires pointer");writeScalar(*p,getReg(frame,ins.right,ins),ins.width?static_cast<std::size_t>(ins.width):8);break;}case 30:break;case 31:throw RuntimeError("unimplemented intrinsic "+std::to_string(ins.symbol));case 32:throw RuntimeError("inline assembly is unavailable in portable NQRI execution");case 33:value=getReg(frame,ins.left,ins);if(auto i=std::get_if<std::int64_t>(&value);i&&*i<0)return value;setReg(frame,ins.dest,value);break;case 34:throw ThrowSignal{asInt(getReg(frame,ins.left,ins))};case 35:{auto index=asInt(getReg(frame,ins.left,ins)),length=asInt(getReg(frame,ins.right,ins));if(index<0||length<0||index>=length)throw RuntimeError("bounds check failed");break;}case 36:break;case 37:{if(ins.width>std::numeric_limits<std::size_t>::max())throw RuntimeError("NQRI allocation exceeds host address range");std::optional<std::size_t> length;if((ins.flags&16u)!=0)length=checkedSize(ins.immediate,"array element count");setReg(frame,ins.dest,blockPtr(std::make_shared<MemoryBlock>(static_cast<std::size_t>(ins.width),length)));break;}case 38:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("load_memory requires pointer");setReg(frame,ins.dest,readScalar(*p,ins.width?static_cast<std::size_t>(ins.width):widthForType(ins.typeCode),ins.typeCode));break;}case 39:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("store_memory requires pointer");writeScalar(*p,getReg(frame,ins.right,ins),ins.width?static_cast<std::size_t>(ins.width):8);break;}case 40:{auto s=std::get_if<SlicePtr>(&getReg(frame,ins.left,ins));if(!s||!*s)throw RuntimeError("slice_data on non-slice");setReg(frame,ins.dest,(*s)->data);break;}case 41:{auto s=std::get_if<SlicePtr>(&getReg(frame,ins.left,ins));if(!s||!*s)throw RuntimeError("slice_len on non-slice");setReg(frame,ins.dest,static_cast<std::int64_t>((*s)->length));break;}case 42:{auto p=std::get_if<PointerPtr>(&getReg(frame,ins.left,ins));if(!p)throw RuntimeError("make_slice data is not pointer");setReg(frame,ins.dest,sliceValue(*p,checkedSize(asInt(getReg(frame,ins.right,ins)),"slice length")));break;}case 43:value=getReg(frame,ins.left,ins);if(ins.typeCode==4)value=asDouble(value);else if(ins.typeCode==2||ins.typeCode==3||(ins.typeCode>=6&&ins.typeCode<=15))value=asInt(value);setReg(frame,ins.dest,value);break;case 45:{auto literal=decodeStringLiteral(module_.source,static_cast<std::size_t>(ins.sourceStart),static_cast<std::size_t>(ins.sourceEnd));auto block=std::make_shared<MemoryBlock>(literal.size(),literal.size());block->buffer=literal;setReg(frame,ins.dest,sliceValue(blockPtr(block),literal.size()));break;}case 46:frame.pendingArgs[checkedSize(ins.immediate,"call argument index")]=getReg(frame,ins.left,ins);break;case 47:{auto idx=checkedSize(ins.immediate,"parameter index");setReg(frame,ins.dest,idx<frame.args.size()?frame.args[idx]:Value{std::int64_t{0}});break;}default:throw RuntimeError("unsupported NIR opcode "+std::to_string(ins.opcode));}frame.pc=next;}return std::int64_t{0};}catch(const RuntimeError& e){const auto& failed=module_.instructions.at(frame.pc);const std::size_t pos=std::min<std::size_t>(static_cast<std::size_t>(failed.sourceStart),module_.sourceText.size());const auto previous=module_.sourceText.rfind('\n',pos);const std::size_t begin=previous==std::string::npos?0:previous+1;const auto following=module_.sourceText.find('\n',pos);const std::size_t finish=following==std::string::npos?module_.sourceText.size():following;const std::size_t count=std::min<std::size_t>(finish-begin,220);throw RuntimeError(std::string(e.what())+" [function="+fn.name+", pc="+std::to_string(frame.pc)+", opcode="+std::to_string(failed.opcode)+", dest="+std::to_string(failed.dest)+", left="+std::to_string(failed.left)+", right="+std::to_string(failed.right)+", symbol="+std::to_string(failed.symbol)+", source="+module_.sourceText.substr(begin,count)+"]");}}
    Module module_;std::uint64_t maxSteps_,steps_=0;std::size_t maxDepth_;
};

static std::vector<std::uint8_t> makeSelftestModule() {
    const std::string source = "addmainmemoryslicecheck";
    constexpr std::uint32_t functionCount = 4;
    constexpr std::uint32_t instructionCount = 20;
    std::vector<std::uint8_t> b(32 + source.size() + functionCount * 40 + instructionCount * 96, 0);
    auto w32=[&](std::size_t o,std::uint32_t v){for(int i=0;i<4;++i)b[o+i]=static_cast<std::uint8_t>((v>>(i*8))&0xff);};
    auto w64=[&](std::size_t o,std::uint64_t v){for(int i=0;i<8;++i)b[o+i]=static_cast<std::uint8_t>((v>>(i*8))&0xff);};
    auto wi64=[&](std::size_t o,std::int64_t v){std::uint64_t raw=0;std::memcpy(&raw,&v,sizeof(raw));w64(o,raw);};
    auto function=[&](std::size_t o,std::uint64_t symbol,std::uint32_t nameStart,std::uint32_t nameEnd,std::uint32_t first,std::uint32_t count,std::uint32_t params){
        w64(o,symbol);w32(o+8,nameStart);w32(o+12,nameEnd);w32(o+16,first);w32(o+20,count);w32(o+24,params);
    };
    auto instruction=[&](std::size_t index,std::uint32_t opcode,std::int64_t dest,std::int64_t left,std::int64_t right,std::int64_t immediate,std::uint64_t symbol,std::uint64_t width,std::uint32_t typeCode=0){
        const std::size_t o=32+source.size()+functionCount*40+index*96;
        w32(o,opcode);w32(o+4,typeCode);wi64(o+16,dest);wi64(o+24,left);wi64(o+32,right);wi64(o+40,immediate);w64(o+48,symbol);w64(o+56,width);
    };
    b[0]='N';b[1]='Q';b[2]='R';b[3]='I';w32(4,2);w32(8,static_cast<std::uint32_t>(source.size()));w32(12,functionCount);w32(16,instructionCount);w32(20,40);w32(24,96);
    std::copy(source.begin(),source.end(),b.begin()+32);
    std::size_t f=32+source.size();
    function(f+0*40,1,0,3,0,4,2);
    function(f+1*40,2,3,7,4,6,0);
    function(f+2*40,3,7,13,10,5,0);
    function(f+3*40,4,13,23,15,5,0);

    instruction(0,47,0,-1,-1,0,101,0);
    instruction(1,47,1,-1,-1,1,102,0);
    instruction(2,6,2,0,1,0,0,0);
    instruction(3,22,-1,2,-1,0,0,0);

    instruction(4,1,0,-1,-1,20,0,0);
    instruction(5,1,1,-1,-1,22,0,0);
    instruction(6,46,-1,0,-1,0,0,0);
    instruction(7,46,-1,1,-1,1,0,0);
    instruction(8,21,2,-1,-1,0,1,2);
    instruction(9,22,-1,2,-1,0,0,0);

    instruction(10,37,0,-1,-1,0,0,8);
    instruction(11,1,1,-1,-1,42,0,0);
    instruction(12,39,-1,0,1,0,0,8);
    instruction(13,38,2,0,-1,0,0,8,3);
    instruction(14,22,-1,2,-1,0,0,0);

    instruction(15,37,0,-1,-1,0,0,16);
    instruction(16,1,1,-1,-1,2,0,0);
    instruction(17,42,2,0,1,0,0,0);
    instruction(18,41,3,2,-1,0,0,0);
    instruction(19,22,-1,3,-1,0,0,0);
    return b;
}
static int selftest() {
    {
        auto invalid=makeSelftestModule();
        invalid[0]='X';
        bool rejected=false;
        try { (void)loadNqri2(invalid); } catch(const RuntimeError&) { rejected=true; }
        if(!rejected) throw RuntimeError("selftest accepted corrupt NQRI magic");
    }
    {
        // Verify both addressable-cell and block-backed f64 bit reinterprets.
        constexpr std::int64_t expectedBits = INT64_C(0x4024000000000000);
        auto memory = blockPtr(std::make_shared<MemoryBlock>(8));
        writeScalar(memory, Value{10.0});
        if (asInt(readScalar(memory, 8, 13)) != expectedBits ||
            asDouble(readScalar(memory, 8, 4)) != 10.0)
            throw RuntimeError("selftest failed: IEEE-754 block bitcast");
        auto floatCell = std::make_shared<Cell>();
        floatCell->value = 10.0;
        if (asInt(readScalar(cellPtr(floatCell), 8, 13)) != expectedBits)
            throw RuntimeError("selftest failed: IEEE-754 cell bitcast");
        auto bitCell = std::make_shared<Cell>();
        bitCell->value = expectedBits;
        if (asDouble(readScalar(cellPtr(bitCell), 8, 4)) != 10.0)
            throw RuntimeError("selftest failed: reverse IEEE-754 bitcast");
    }
    auto m=loadNqri2(makeSelftestModule());
    Vm vm(std::move(m));
    auto expectInt=[&](const char* name,std::int64_t expected){
        auto result=vm.invoke(name);
        if(!std::holds_alternative<std::int64_t>(result)||std::get<std::int64_t>(result)!=expected)throw RuntimeError(std::string("selftest failed for ")+name);
    };
    expectInt("main",42);
    expectInt("memory",42);
    expectInt("slicecheck",2);
    std::cout<<"native NQRI selftest passed\n";
    return 0;
}
static std::string valueText(const Value&v){if(auto i=std::get_if<std::int64_t>(&v))return std::to_string(*i);if(auto d=std::get_if<double>(&v)){std::ostringstream s;s<<*d;return s.str();}if(std::holds_alternative<std::monostate>(v))return"null";if(std::holds_alternative<PointerPtr>(v))return"<pointer>";return"<slice>";}

} // namespace noqeri::nqri

