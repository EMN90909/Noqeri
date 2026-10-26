#include "noe.hpp"
#include <algorithm>
#include <sstream>

namespace noe {
namespace {
std::string opText(TokenKind k){switch(k){case TokenKind::Plus:return"+";case TokenKind::Minus:return"-";case TokenKind::Star:return"*";case TokenKind::Slash:return"/";case TokenKind::Percent:return"%";case TokenKind::Bang:return"!";case TokenKind::Ampersand:return"&";case TokenKind::EqualEqual:return"==";case TokenKind::BangEqual:return"!=";case TokenKind::Less:return"<";case TokenKind::LessEqual:return"<=";case TokenKind::Greater:return">";case TokenKind::GreaterEqual:return">=";case TokenKind::AndAnd:return"&&";case TokenKind::OrOr:return"||";case TokenKind::Try:return"try";default:return"?";}}
std::string literalText(const NirValue&v){if(std::holds_alternative<std::monostate>(v))return"null";if(auto p=std::get_if<std::int64_t>(&v))return std::to_string(*p);if(auto p=std::get_if<double>(&v)){std::ostringstream o;o<<*p;return o.str();}if(auto p=std::get_if<bool>(&v))return*p?"true":"false";return '"'+std::get<std::string>(v)+'"';}
std::pair<std::size_t,bool> memoryInfo(const ExprPtr&e){if(auto x=std::dynamic_pointer_cast<IndexExpr>(e))return{x->elementSize,x->volatileAccess};if(auto x=std::dynamic_pointer_cast<MemberExpr>(e))return{x->fieldSize?x->fieldSize:8,x->volatileAccess};if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e);x&&x->op==TokenKind::Star)return{x->memoryWidth,x->volatileAccess};return{8,false};}
std::string calleeName(const ExprPtr&expr){if(auto n=std::dynamic_pointer_cast<NameExpr>(expr))return n->name;if(auto m=std::dynamic_pointer_cast<MemberExpr>(expr))if(auto n=std::dynamic_pointer_cast<NameExpr>(m->object))return n->name+"."+m->member;return{};}
}

Reg Lowerer::emit(NirFunction&fn,NirInstruction ins){if(ins.dest)fn.nextReg=std::max(fn.nextReg,Reg(*ins.dest+1));fn.code.push_back(std::move(ins));return fn.code.back().dest.value_or(0);}
void Lowerer::closeOwnedLists(NirFunction&fn,std::size_t boundary){
    for(std::size_t index=ownedListVariables_.size();index>boundary;--index){
        NirInstruction load;load.op=NirOp::Load;load.dest=fn.nextReg++;
        load.text=ownedListVariables_[index-1];emit(fn,load);
        NirInstruction close;close.op=NirOp::Call;close.dest=fn.nextReg++;
        close.text="nq_list_close";close.args={*load.dest};emit(fn,close);
    }
}
NirProgram Lowerer::lower(const Program&program){
    ownedListVariables_.clear();
    NirProgram out;out.entry.name="__entry";
    for(const auto&s:program.statements){
        if(auto f=std::dynamic_pointer_cast<FunctionStmt>(s)){
            NirFunction nf;nf.name=f->name;nf.genericParams=f->genericParams;nf.isExtern=f->isExtern||f->isForward;nf.isExport=f->isExport;for(const auto&p:f->params)nf.params.push_back(p.name);
            if(!f->isExtern&&!f->isForward){
                ownedListVariables_.clear();
                for(const auto&x:f->body->statements)lowerStmt(nf,x);
                closeOwnedLists(nf,0);
                if(nf.code.empty()||nf.code.back().op!=NirOp::Return){NirInstruction r;r.op=NirOp::Return;nf.code.push_back(r);}
            }
            out.functions.push_back(std::move(nf));
        }else if(!std::dynamic_pointer_cast<RecordStmt>(s)&&!std::dynamic_pointer_cast<ImportStmt>(s)&&!std::dynamic_pointer_cast<ModuleStmt>(s))lowerStmt(out.entry,s);
    }
    closeOwnedLists(out.entry,0);
    ownedListVariables_.clear();
    NirInstruction r;r.op=NirOp::Return;out.entry.code.push_back(r);return out;
}
void Lowerer::lowerStmt(NirFunction&fn,const StmtPtr&stmt){
    if(auto s=std::dynamic_pointer_cast<LetStmt>(stmt)){
        if(s->partition){
            // Persist compiler-validated placement in NIR. No local array
            // is allocated: executing a distributed array as local storage
            // would silently corrupt its meaning. Backends must fail closed
            // until they implement actual topology-backed lowering.
            fn.partitions.push_back(NirPartitionDescriptor{s->name,
                s->partition->strategy,s->partition->nodes,
                s->partition->elements});
            return;
        }
        if(s->inferredGrowableIntList){
            const auto literal=std::dynamic_pointer_cast<ArrayExpr>(s->initializer);
            NirInstruction open;open.op=NirOp::Call;open.dest=fn.nextReg++;
            open.text="nq_list_new";emit(fn,open);
            NirInstruction bind;bind.op=NirOp::Store;bind.text=s->name;
            bind.args={*open.dest};emit(fn,bind);
            ownedListVariables_.push_back(s->name);
            for(const auto& element:literal->elements){
                const Reg value=lowerExpr(fn,element);
                NirInstruction append;append.op=NirOp::Call;append.dest=fn.nextReg++;
                append.text="nq_list_append";append.args={*open.dest,value};emit(fn,append);
            }
            return;
        }
        Reg r=0;if(s->zeroInitialized){NirInstruction z;if(s->zeroRequiresStorage){z.op=NirOp::StackAlloc;z.dest=fn.nextReg++;z.width=std::max<std::size_t>(1,s->zeroStorageBytes);}else{z.op=NirOp::Call;z.dest=fn.nextReg++;z.text=s->annotation.value_or("int");}emit(fn,z);r=*z.dest;}else r=lowerExpr(fn,s->initializer);NirInstruction i;i.op=NirOp::Store;i.text=s->name;i.args={r};emit(fn,std::move(i));return;}
    if(auto s=std::dynamic_pointer_cast<ExprStmt>(stmt)){lowerExpr(fn,s->expr);return;}
    if(auto s=std::dynamic_pointer_cast<BlockStmt>(stmt)){
        const std::size_t boundary=ownedListVariables_.size();
        for(auto&x:s->statements)lowerStmt(fn,x);
        closeOwnedLists(fn,boundary);
        ownedListVariables_.resize(boundary);
        return;
    }
    if(auto s=std::dynamic_pointer_cast<ReturnStmt>(stmt)){
        NirInstruction i;i.op=NirOp::Return;
        if(s->value)i.args={lowerExpr(fn,s->value)};
        closeOwnedLists(fn,0);
        emit(fn,std::move(i));return;
    }
    if(auto s=std::dynamic_pointer_cast<ThrowStmt>(stmt)){NirInstruction i;i.op=NirOp::Throw;i.args={lowerExpr(fn,s->value)};emit(fn,std::move(i));return;}
    if(auto s=std::dynamic_pointer_cast<IfStmt>(stmt)){Reg c=lowerExpr(fn,s->condition);NirInstruction j;j.op=NirOp::JumpIfFalse;j.args={c};auto jp=fn.code.size();emit(fn,std::move(j));lowerStmt(fn,s->thenBranch);if(s->elseBranch){NirInstruction end;end.op=NirOp::Jump;auto ep=fn.code.size();emit(fn,std::move(end));fn.code[jp].target=fn.code.size();lowerStmt(fn,s->elseBranch);fn.code[ep].target=fn.code.size();}else fn.code[jp].target=fn.code.size();return;}
    if(auto s=std::dynamic_pointer_cast<BreakStmt>(stmt)){NirInstruction jump;jump.op=NirOp::Jump;const auto pos=fn.code.size();emit(fn,std::move(jump));if(!breakJumps_.empty())breakJumps_.back().push_back(pos);return;}
    if(auto s=std::dynamic_pointer_cast<ContinueStmt>(stmt)){NirInstruction jump;jump.op=NirOp::Jump;const auto pos=fn.code.size();emit(fn,std::move(jump));if(!continueJumps_.empty())continueJumps_.back().push_back(pos);return;}
    if(auto s=std::dynamic_pointer_cast<WhileStmt>(stmt)){
        const auto start=fn.code.size();
        Reg c=lowerExpr(fn,s->condition);
        NirInstruction j;j.op=NirOp::JumpIfFalse;j.args={c};const auto exitJump=fn.code.size();emit(fn,std::move(j));
        breakJumps_.push_back({});continueJumps_.push_back({});
        lowerStmt(fn,s->body);
        for(const auto pos:continueJumps_.back())fn.code[pos].target=start;
        NirInstruction back;back.op=NirOp::Jump;back.target=start;emit(fn,std::move(back));
        const auto end=fn.code.size();
        fn.code[exitJump].target=end;
        for(const auto pos:breakJumps_.back())fn.code[pos].target=end;
        breakJumps_.pop_back();continueJumps_.pop_back();
        return;
    }
}
Reg Lowerer::lowerAddress(NirFunction&fn,const ExprPtr&expr){
    if(auto n=std::dynamic_pointer_cast<NameExpr>(expr)){
        // Aggregate locals already live in StackAlloc backing storage. The
        // Store/Load variable carries its address. AddressOf on the VM's
        // scalar variable cell only gives eight bytes, so accessing any record
        // field after offset 0 wrote into the host unordered_map node.
        if(n->aggregateStorage){
            NirInstruction load;load.op=NirOp::Load;load.dest=fn.nextReg++;
            load.text=n->name;emit(fn,load);return *load.dest;
        }
        NirInstruction i;i.op=NirOp::AddressOf;i.dest=fn.nextReg++;
        i.text=n->name;emit(fn,i);return *i.dest;
    }
    if(auto u=std::dynamic_pointer_cast<UnaryExpr>(expr);u&&u->op==TokenKind::Star){Reg base=lowerExpr(fn,u->operand);NirInstruction check;check.op=NirOp::CheckNonNull;check.args={base};emit(fn,check);return base;}
    if(auto x=std::dynamic_pointer_cast<IndexExpr>(expr)){
        Reg base=lowerExpr(fn,x->object);
        Reg idx=lowerExpr(fn,x->index);
        if(x->baseIsSlice){
            NirInstruction nonnull;nonnull.op=NirOp::CheckNonNull;nonnull.args={base};emit(fn,nonnull);
            NirInstruction length;length.op=NirOp::SliceLen;length.dest=fn.nextReg++;length.args={base};emit(fn,length);
            NirInstruction bounds;bounds.op=NirOp::CheckBounds;bounds.args={idx,*length.dest};emit(fn,bounds);
            NirInstruction data;data.op=NirOp::SliceData;data.dest=fn.nextReg++;data.args={base};emit(fn,data);base=*data.dest;
            NirInstruction dataNonNull;dataNonNull.op=NirOp::CheckNonNull;dataNonNull.args={base};emit(fn,dataNonNull);
        }else{
            if(x->hasFixedBound){NirInstruction length;length.op=NirOp::Const;length.dest=fn.nextReg++;length.literal=static_cast<std::int64_t>(x->fixedBound);emit(fn,length);NirInstruction bounds;bounds.op=NirOp::CheckBounds;bounds.args={idx,*length.dest};emit(fn,bounds);}
            else{NirInstruction nonnull;nonnull.op=NirOp::CheckNonNull;nonnull.args={base};emit(fn,nonnull);}
        }
        NirInstruction i;i.op=NirOp::PtrOffset;i.dest=fn.nextReg++;i.args={base,idx};i.width=x->elementSize;emit(fn,i);return*i.dest;
    }
    if(auto m=std::dynamic_pointer_cast<MemberExpr>(expr)){Reg base=m->baseIsPointer?lowerExpr(fn,m->object):lowerAddress(fn,m->object);if(m->baseIsPointer){NirInstruction check;check.op=NirOp::CheckNonNull;check.args={base};emit(fn,check);}NirInstruction i;i.op=NirOp::PtrOffset;i.dest=fn.nextReg++;i.args={base};i.target=m->offset;i.width=1;emit(fn,i);return*i.dest;}
    NirInstruction z;z.op=NirOp::Const;z.dest=fn.nextReg++;z.literal=std::int64_t(0);emit(fn,z);return*z.dest;
}
Reg Lowerer::lowerExpr(NirFunction&fn,const ExprPtr&expr){
    if(auto e=std::dynamic_pointer_cast<LiteralExpr>(expr)){NirInstruction i;i.op=NirOp::Const;i.dest=fn.nextReg++;i.literal=e->value;emit(fn,i);return*i.dest;}
    if(auto e=std::dynamic_pointer_cast<ArrayExpr>(expr)){
        NirInstruction alloc;alloc.op=NirOp::StackAlloc;alloc.dest=fn.nextReg++;alloc.width=std::max<std::size_t>(1,e->elementSize*e->elements.size());emit(fn,alloc);Reg base=*alloc.dest;
        for(std::size_t index=0;index<e->elements.size();++index){Reg value=lowerExpr(fn,e->elements[index]);NirInstruction address;address.op=NirOp::PtrOffset;address.dest=fn.nextReg++;address.args={base};address.target=index*e->elementSize;address.width=1;emit(fn,address);NirInstruction store;store.op=NirOp::StoreMemory;store.args={*address.dest,value};store.width=e->elementSize;emit(fn,store);}return base;
    }
    if(auto e=std::dynamic_pointer_cast<NameExpr>(expr)){NirInstruction i;i.op=NirOp::Load;i.dest=fn.nextReg++;i.text=e->name;emit(fn,i);return*i.dest;}
    if(auto e=std::dynamic_pointer_cast<UnaryExpr>(expr)){
        if(e->op==TokenKind::Ampersand)return lowerAddress(fn,e->operand);
        if(e->op==TokenKind::Star){Reg a=lowerExpr(fn,e->operand);NirInstruction check;check.op=NirOp::CheckNonNull;check.args={a};emit(fn,check);NirInstruction i;i.op=NirOp::LoadMemory;i.dest=fn.nextReg++;i.args={a};i.width=e->memoryWidth;i.isVolatile=e->volatileAccess;i.type=e->memoryType;emit(fn,i);return*i.dest;}
        if(e->op==TokenKind::Question){
            // The operand is a borrowed pointer to an Outcome/Result record.
            // Evaluate once, check the ok byte, early-return the same pointer
            // on failure, or load the typed scalar value on success.
            const Reg base=lowerExpr(fn,e->operand);
            NirInstruction nonnull;nonnull.op=NirOp::CheckNonNull;nonnull.args={base};emit(fn,nonnull);
            NirInstruction flagPtr;flagPtr.op=NirOp::PtrOffset;flagPtr.dest=fn.nextReg++;
            flagPtr.args={base};flagPtr.target=e->tryOkOffset;flagPtr.width=1;emit(fn,flagPtr);
            NirInstruction flag;flag.op=NirOp::LoadMemory;flag.dest=fn.nextReg++;
            flag.args={*flagPtr.dest};flag.width=1;emit(fn,flag);
            NirInstruction bad;bad.op=NirOp::JumpIfFalse;bad.args={*flag.dest};
            const auto badIndex=fn.code.size();emit(fn,bad);
            NirInstruction valPtr;valPtr.op=NirOp::PtrOffset;valPtr.dest=fn.nextReg++;
            valPtr.args={base};valPtr.target=e->tryValueOffset;valPtr.width=1;emit(fn,valPtr);
            NirInstruction load;load.op=NirOp::LoadMemory;load.dest=fn.nextReg++;
            load.args={*valPtr.dest};load.width=e->tryValueWidth;emit(fn,load);
            NirInstruction skip;skip.op=NirOp::Jump;const auto skipIndex=fn.code.size();emit(fn,skip);
            fn.code[badIndex].target=fn.code.size();
            NirInstruction propagate;propagate.op=NirOp::Return;propagate.args={base};emit(fn,propagate);
            fn.code[skipIndex].target=fn.code.size();
            return *load.dest;
        }
        if(e->op==TokenKind::Try){Reg a=lowerExpr(fn,e->operand);NirInstruction i;i.op=NirOp::Try;i.dest=fn.nextReg++;i.args={a};emit(fn,i);return*i.dest;}
        Reg a=lowerExpr(fn,e->operand);NirInstruction i;i.op=NirOp::Unary;i.dest=fn.nextReg++;i.text=opText(e->op);i.args={a};emit(fn,i);return*i.dest;
    }
    if(auto e=std::dynamic_pointer_cast<MemberExpr>(expr)){Reg a=lowerAddress(fn,e);NirInstruction i;i.op=NirOp::LoadMemory;i.dest=fn.nextReg++;i.args={a};i.width=e->fieldSize?e->fieldSize:8;i.isVolatile=e->volatileAccess;i.type=e->fieldType;emit(fn,i);return*i.dest;}
    if(auto e=std::dynamic_pointer_cast<IndexExpr>(expr)){Reg a=lowerAddress(fn,e);NirInstruction i;i.op=NirOp::LoadMemory;i.dest=fn.nextReg++;i.args={a};i.width=e->elementSize;i.isVolatile=e->volatileAccess;i.type=e->elementType;emit(fn,i);return*i.dest;}
    if(auto e=std::dynamic_pointer_cast<CastExpr>(expr)){Reg a=lowerExpr(fn,e->value);NirInstruction i;i.op=NirOp::Cast;i.dest=fn.nextReg++;i.text=e->typeName;i.args={a};emit(fn,i);return*i.dest;}
    if(auto e=std::dynamic_pointer_cast<BinaryExpr>(expr)){
        if(e->op==TokenKind::Equal){Reg r=lowerExpr(fn,e->right);if(auto n=std::dynamic_pointer_cast<NameExpr>(e->left)){NirInstruction st;st.op=NirOp::Store;st.text=n->name;st.args={r};emit(fn,st);return r;}Reg addr=lowerAddress(fn,e->left);auto info=memoryInfo(e->left);NirInstruction st;st.op=NirOp::StoreMemory;st.args={addr,r};st.width=info.first;st.isVolatile=info.second;emit(fn,st);return r;}
        if(e->op==TokenKind::AndAnd||e->op==TokenKind::OrOr||e->op==TokenKind::QuestionQuestion){
            Reg left=lowerExpr(fn,e->left);
            const std::string temp="$noqeri.logic."+std::to_string(fn.code.size())+"."+std::to_string(fn.nextReg);
            NirInstruction saveLeft;saveLeft.op=NirOp::Store;saveLeft.text=temp;saveLeft.args={left};emit(fn,std::move(saveLeft));
            NirInstruction test;test.op=NirOp::JumpIfFalse;test.args={left};const auto testPos=fn.code.size();emit(fn,std::move(test));
            if(e->op==TokenKind::AndAnd){Reg right=lowerExpr(fn,e->right);NirInstruction saveRight;saveRight.op=NirOp::Store;saveRight.text=temp;saveRight.args={right};emit(fn,std::move(saveRight));fn.code[testPos].target=fn.code.size();}
            else{NirInstruction skipRight;skipRight.op=NirOp::Jump;const auto skipPos=fn.code.size();emit(fn,std::move(skipRight));fn.code[testPos].target=fn.code.size();Reg right=lowerExpr(fn,e->right);NirInstruction saveRight;saveRight.op=NirOp::Store;saveRight.text=temp;saveRight.args={right};emit(fn,std::move(saveRight));fn.code[skipPos].target=fn.code.size();}
            NirInstruction result;result.op=NirOp::Load;result.dest=fn.nextReg++;result.text=temp;emit(fn,result);return*result.dest;
        }
        Reg l=lowerExpr(fn,e->left),r=lowerExpr(fn,e->right);NirInstruction i;i.op=NirOp::Binary;i.dest=fn.nextReg++;i.text=opText(e->op);i.args={l,r};emit(fn,i);return*i.dest;
    }
    if(auto e=std::dynamic_pointer_cast<CallExpr>(expr)){
        if(e->growableListMethod){
            auto member=std::dynamic_pointer_cast<MemberExpr>(e->callee);
            Reg list=lowerExpr(fn,member->object);
            NirInstruction call;call.op=NirOp::Call;
            call.dest=fn.nextReg++;
            if(e->builtinText=="append"){
                call.text="nq_list_append";
                call.args={list,lowerExpr(fn,e->args.front())};
            }else{
                call.text="nq_list_length";call.args={list};
            }
            emit(fn,call);return *call.dest;
        }
        std::string name=calleeName(e->callee);
        if(name=="sizeOf"||name=="alignOf"){
            NirInstruction i;i.op=NirOp::Const;i.dest=fn.nextReg++;
            i.literal=static_cast<std::int64_t>(e->builtinCount);
            emit(fn,i);return *i.dest;
        }
        if(name=="len"){
            if(e->builtinCountKnown){NirInstruction i;i.op=NirOp::Const;i.dest=fn.nextReg++;i.literal=static_cast<std::int64_t>(e->builtinCount);emit(fn,i);return*i.dest;}
            Reg slice=lowerExpr(fn,e->args[0]);NirInstruction check;check.op=NirOp::CheckNonNull;check.args={slice};emit(fn,check);NirInstruction i;i.op=NirOp::SliceLen;i.dest=fn.nextReg++;i.args={slice};emit(fn,i);return*i.dest;
        }
        if(name=="slice"){
            Reg data=lowerExpr(fn,e->args[0]);
            if(e->builtinSliceSource){NirInstruction d;d.op=NirOp::SliceData;d.dest=fn.nextReg++;d.args={data};emit(fn,d);data=*d.dest;}
            Reg length=0;
            if(e->args.size()==2)length=lowerExpr(fn,e->args[1]);else{NirInstruction c;c.op=NirOp::Const;c.dest=fn.nextReg++;c.literal=static_cast<std::int64_t>(e->builtinCount);emit(fn,c);length=*c.dest;}
            NirInstruction i;i.op=NirOp::MakeSlice;i.dest=fn.nextReg++;i.args={data,length};i.width=e->builtinWidth;emit(fn,i);return*i.dest;
        }
        auto atomic=[&](NirOp op,std::size_t firstArg=0)->Reg{NirInstruction i;i.op=op;if(op!=NirOp::AtomicStore&&op!=NirOp::AtomicFence)i.dest=fn.nextReg++;i.width=e->builtinWidth;for(std::size_t a=firstArg;a<e->args.size();++a)i.args.push_back(lowerExpr(fn,e->args[a]));emit(fn,i);if(i.dest)return*i.dest;NirInstruction z;z.op=NirOp::Const;z.dest=fn.nextReg++;z.literal=std::monostate{};emit(fn,z);return*z.dest;};
        if(name=="atomic.load"||name=="atomicLoad")return atomic(NirOp::AtomicLoad);
        if(name=="atomic.store"||name=="atomicStore")return atomic(NirOp::AtomicStore);
        if(name=="atomic.exchange"||name=="atomicExchange")return atomic(NirOp::AtomicExchange);
        if(name=="atomic.compareExchange"||name=="atomicCompareExchange")return atomic(NirOp::AtomicCompareExchange);
        if(name=="atomic.fence"||name=="atomicFence")return atomic(NirOp::AtomicFence);
        if(name=="asm"){NirInstruction i;i.op=NirOp::InlineAsm;i.dest=fn.nextReg++;i.text=e->builtinText;emit(fn,i);return*i.dest;}
        if(name=="intrinsic"){NirInstruction i;i.op=NirOp::Intrinsic;i.dest=fn.nextReg++;i.text=e->builtinText;for(std::size_t a=1;a<e->args.size();++a)i.args.push_back(lowerExpr(fn,e->args[a]));emit(fn,i);return*i.dest;}
        NirInstruction i;i.op=NirOp::Call;i.dest=fn.nextReg++;i.text=name.empty()?"<call>":name;for(auto&a:e->args)i.args.push_back(lowerExpr(fn,a));emit(fn,i);return*i.dest;
    }
    NirInstruction i;i.op=NirOp::Const;i.dest=fn.nextReg++;i.literal=std::monostate{};emit(fn,i);return*i.dest;
}

std::string printNir(const NirProgram&p){
    std::ostringstream out;auto one=[&](const NirFunction&f){if(f.isExport)out<<"export ";if(f.isExtern)out<<"extern ";out<<"function "<<f.name;if(!f.genericParams.empty()){out<<'<';for(std::size_t g=0;g<f.genericParams.size();++g){if(g)out<<", ";out<<f.genericParams[g];}out<<'>'; }out<<'(';for(std::size_t i=0;i<f.params.size();++i){if(i)out<<", ";out<<f.params[i];}out<<") {\n";
        for(const auto&part:f.partitions)out<<"  @partition "<<part.name<<" strategy="<<part.strategy<<" nodes="<<part.nodes<<" elements="<<part.elements<<"\n";
        for(std::size_t pc=0;pc<f.code.size();++pc){const auto&i=f.code[pc];out<<"  "<<pc<<": ";if(i.dest)out<<'%'<<*i.dest<<" = ";switch(i.op){
            case NirOp::Const:out<<"const "<<literalText(i.literal);break;case NirOp::Load:out<<"load "<<i.text;break;case NirOp::Store:out<<"store "<<i.text<<", %"<<i.args[0];break;case NirOp::Unary:out<<"unary "<<i.text<<" %"<<i.args[0];break;case NirOp::Binary:out<<"binary "<<i.text<<" %"<<i.args[0]<<", %"<<i.args[1];break;case NirOp::Cast:out<<"cast "<<i.text<<" %"<<i.args[0];break;case NirOp::AddressOf:out<<"address_of "<<i.text;break;
            case NirOp::LoadMemory:out<<(i.isVolatile?"volatile_":"")<<"load_memory."<<i.width<<" %"<<i.args[0];break;case NirOp::StoreMemory:out<<(i.isVolatile?"volatile_":"")<<"store_memory."<<i.width<<" %"<<i.args[0]<<", %"<<i.args[1];break;case NirOp::PtrOffset:out<<"ptr_offset %"<<i.args[0];if(i.args.size()>1)out<<", %"<<i.args[1]<<" * "<<i.width;else out<<", +"<<i.target;break;case NirOp::StackAlloc:out<<"stack_alloc "<<i.width;break;
            case NirOp::MakeSlice:out<<"make_slice %"<<i.args[0]<<", %"<<i.args[1];break;case NirOp::SliceData:out<<"slice_data %"<<i.args[0];break;case NirOp::SliceLen:out<<"slice_len %"<<i.args[0];break;case NirOp::CheckNonNull:out<<"check_non_null %"<<i.args[0];break;case NirOp::CheckBounds:out<<"check_bounds %"<<i.args[0]<<", %"<<i.args[1];break;
            case NirOp::AtomicLoad:out<<"atomic_load."<<i.width<<" %"<<i.args[0];break;case NirOp::AtomicStore:out<<"atomic_store."<<i.width<<" %"<<i.args[0]<<", %"<<i.args[1];break;case NirOp::AtomicExchange:out<<"atomic_exchange."<<i.width<<" %"<<i.args[0]<<", %"<<i.args[1];break;case NirOp::AtomicCompareExchange:out<<"atomic_compare_exchange."<<i.width<<" %"<<i.args[0]<<", %"<<i.args[1]<<", %"<<i.args[2];break;case NirOp::AtomicFence:out<<"atomic_fence";break;
            case NirOp::Intrinsic:out<<"intrinsic \""<<i.text<<"\"";break;case NirOp::InlineAsm:out<<"asm \""<<i.text<<"\"";break;case NirOp::Try:out<<"try %"<<i.args[0];break;case NirOp::Throw:out<<"throw %"<<i.args[0];break;
            case NirOp::Call:out<<"call "<<i.text<<'(';for(std::size_t a=0;a<i.args.size();++a){if(a)out<<", ";out<<'%'<<i.args[a];}out<<')';break;case NirOp::Jump:out<<"jump "<<i.target;break;case NirOp::JumpIfFalse:out<<"jump_if_false %"<<i.args[0]<<", "<<i.target;break;case NirOp::Return:out<<"return"<<(i.args.empty()?"":" %"+std::to_string(i.args[0]));break;case NirOp::Nop:out<<"nop";break;
        }out<<"\n";}out<<"}\n";};one(p.entry);for(const auto&f:p.functions)one(f);return out.str();
}
} // namespace noe