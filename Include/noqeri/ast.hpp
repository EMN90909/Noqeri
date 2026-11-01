#pragma once
#include "diagnostics.hpp"
#include "types.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace noe {

enum class TokenKind {
    Eof, Identifier, Integer, Float, String, InterpolatedString,
    LineComment, BlockComment,
    Let, Const, Function, If, Else, While, Repeat, Break, Continue, Return, True, False, Null,
    Import, Module, Record, Class, Extern, Export, Volatile, As, Throw, Try, Unsafe,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Colon, Semicolon, Dot, At,
    Plus, Minus, Star, Slash, Percent, Ampersand,
    Bang, BangEqual, Equal, EqualEqual,
    Less, LessEqual, Greater, GreaterEqual,
    AndAnd, OrOr, Question, QuestionQuestion
};

struct Token { TokenKind kind=TokenKind::Eof; std::string lexeme; Span span; };
const char* tokenKindName(TokenKind kind);

using Literal = std::variant<std::monostate,std::int64_t,double,bool,std::string>;

struct Expr { virtual ~Expr()=default; Span span; };
using ExprPtr = std::shared_ptr<Expr>;
struct LiteralExpr final : Expr { Literal value; };
struct NameExpr final : Expr { std::string name; bool aggregateStorage=false; };
struct UnaryExpr final : Expr { TokenKind op; ExprPtr operand; std::size_t memoryWidth=8; bool volatileAccess=false; Type memoryType{}; std::size_t tryOkOffset=0,tryValueOffset=0,tryValueWidth=0; };
struct BinaryExpr final : Expr { ExprPtr left; TokenKind op; ExprPtr right; };
// An explicit, borrowed contiguous cursor contract. The frontend validates
// the four fields before setting enabled; NIR never guesses record offsets.
struct CursorLayout { bool enabled=false; std::size_t data=0,length=0,front=0,back=0; };
struct CallExpr final : Expr { CursorLayout cursor; ExprPtr callee; std::vector<ExprPtr> args; std::size_t builtinWidth=8; std::size_t builtinCount=0; bool builtinCountKnown=false; bool builtinSliceSource=false; std::string builtinText; bool growableListMethod=false; Type builtinResultType{}; };
struct CastExpr final : Expr { ExprPtr value; std::string typeName; };
struct IndexExpr final : Expr { CursorLayout cursor; ExprPtr object,index; std::size_t elementSize=1; bool volatileAccess=false; bool baseIsSlice=false; bool hasFixedBound=false; std::size_t fixedBound=0; bool growableListRead=false; Type elementType{}; };
struct MemberExpr final : Expr { ExprPtr object; std::string member; std::size_t offset=0; std::size_t fieldSize=0; bool volatileAccess=false; bool baseIsPointer=false; Type fieldType{}; };
struct ArrayExpr final : Expr { std::vector<ExprPtr> elements; std::size_t elementSize=0; };
struct InterpolatedStringExpr final : Expr {
    std::vector<ExprPtr> parts;
    std::vector<Type> partTypes;
};
// Inferred, stack-backed structural record. Field offsets are computed by
// type checking before NIR lowering; values are evaluated in source order.
struct AnonymousField { std::string name; ExprPtr initializer; Span span; Type type{}; std::size_t offset=0,size=0; };
struct AnonymousRecordExpr final : Expr { std::vector<AnonymousField> fields; std::string typeName; std::size_t size=0,alignment=1; };

struct Stmt { virtual ~Stmt()=default; Span span; };
using StmtPtr = std::shared_ptr<Stmt>;
struct ExprStmt final : Stmt { ExprPtr expr; };
struct PartitionAttribute { std::string strategy; std::uint32_t nodes=0; std::size_t elements=0; };
struct LetStmt final : Stmt { bool inferredGrowableIntList=false; Type inferredGrowableElementType{}; bool borrowedListLoopCapture=false; std::optional<PartitionAttribute> partition; bool isConst=false; std::string name; std::optional<std::string> annotation; ExprPtr initializer; bool zeroInitialized=false; bool zeroRequiresStorage=false; std::size_t zeroStorageBytes=0; };
struct BlockStmt final : Stmt { std::vector<StmtPtr> statements; bool isUnsafe=false; };
struct IfStmt final : Stmt { ExprPtr condition; StmtPtr thenBranch; StmtPtr elseBranch; };
struct WhileStmt final : Stmt { ExprPtr condition; StmtPtr body; };
struct BreakStmt final : Stmt {};
struct ContinueStmt final : Stmt {};
struct ReturnStmt final : Stmt { ExprPtr value; };
struct ThrowStmt final : Stmt { ExprPtr value; };
struct Parameter { std::string name; std::optional<std::string> annotation; Span span; };
struct FunctionStmt final : Stmt { std::string name; std::vector<std::string> genericParams; std::unordered_map<std::string,std::vector<std::string>> genericConstraints; std::vector<Parameter> params; std::optional<std::string> returnType; std::shared_ptr<BlockStmt> body; bool isExtern=false; bool isExport=false; bool isForward=false; };
struct RecordField { std::string name,typeName; Span span; std::size_t offset=0,size=0; };
struct RecordStmt final : Stmt { std::string name; std::vector<std::string> genericParams; std::vector<RecordField> fields; std::size_t size=0,alignment=1; };
struct ImportStmt final : Stmt { std::string path; };
struct ModuleStmt final : Stmt { std::string name; };
struct Program { std::vector<StmtPtr> statements; };

} // namespace noe
