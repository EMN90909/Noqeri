#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace noe {

enum class TypeKind {
    Unknown,Void,Null,Bool,
    I8,I16,I32,I64,U8,U16,U32,U64,Isize,Usize,Int,Float,String,
    Pointer,Record,Array,Slice,Generic,List
};

struct Type {
    TypeKind kind=TypeKind::Unknown;
    std::string recordName;
    std::string genericName;
    std::shared_ptr<Type> pointee;
    std::shared_ptr<Type> element;
    std::size_t count=0;
    std::size_t recordSize=0;
    std::size_t recordAlignment=1;
    bool isVolatile=false;
    std::string name() const;
    bool isInteger() const;
    bool isNumeric() const { return isInteger() || kind==TypeKind::Float; }
    bool isPointer() const { return kind==TypeKind::Pointer; }
    bool isAggregate() const { return kind==TypeKind::Array || kind==TypeKind::Slice || kind==TypeKind::Record; }
    std::size_t size() const;
    std::size_t alignment() const;
    bool operator==(const Type& o) const;
    bool operator!=(const Type& o) const { return !(*this==o); }
};

Type typeFromName(const std::string& name);
bool canAssign(Type target,Type value);

} // namespace noe
