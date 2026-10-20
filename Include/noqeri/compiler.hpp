#pragma once
#include "ast.hpp"
#include "diagnostics.hpp"
#include "modules.hpp"
#include "nir.hpp"
#include "types.hpp"
#include "version.hpp"
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace noe {

class Lexer { public: Lexer(std::string source,Diagnostics& diagnostics); Lexer(std::string source,Diagnostics& diagnostics,SourceId sourceId,bool preserveTrivia=false); std::vector<Token> lex(); private: char peek(std::size_t offset=0) const; bool match(char expected); char advance(); void skipWhitespaceAndComments(); Token scanToken(); Token identifier(); Token number(); Token stringLiteral(); Token make(TokenKind kind,std::size_t start,std::size_t line,std::size_t col); std::string source_; Diagnostics& diagnostics_; std::size_t current_=0,line_=1,column_=1; SourceId sourceId_=0; bool preserveTrivia_=false; };

class Parser { public: Parser(std::vector<Token> tokens,Diagnostics& diagnostics); Program parse(); private: StmtPtr declaration(); StmtPtr functionDeclaration(bool isExtern=false,bool isExport=false); StmtPtr recordDeclaration(); StmtPtr importDeclaration(); StmtPtr moduleDeclaration(); StmtPtr letDeclaration(bool isConst); StmtPtr statement(); StmtPtr ifStatement(); StmtPtr whileStatement(); StmtPtr repeatStatement(); StmtPtr breakStatement(); StmtPtr continueStatement(); StmtPtr returnStatement(); StmtPtr throwStatement(); std::shared_ptr<BlockStmt> block(); StmtPtr expressionStatement(); ExprPtr expression(); ExprPtr assignment(); ExprPtr logicalOr(); ExprPtr logicalAnd(); ExprPtr equality(); ExprPtr comparison(); ExprPtr term(); ExprPtr factor(); ExprPtr unary(); ExprPtr cast(); ExprPtr call(); ExprPtr primary(); std::string parseTypeName(); std::vector<std::string> parseGenericParams(); bool match(std::initializer_list<TokenKind> kinds); bool check(TokenKind kind) const; const Token& advance(); const Token& previous() const; const Token& peek() const; const Token& consume(TokenKind kind,const std::string& message); const Token& consumeName(const std::string& message); void synchronize(); std::vector<Token> tokens_; Diagnostics& diagnostics_; std::size_t current_=0; };

struct FunctionType {
    std::vector<Type> params;
    Type result;
    std::vector<std::string> genericParams;
    bool isExtern=false;
    bool isExport=false;
    bool isForward=false;
    std::unordered_map<std::string,std::vector<std::string>> genericConstraints;

    FunctionType()=default;
    FunctionType(std::vector<Type> p,Type r,std::vector<std::string> generic={},bool external=false,bool exported=false,std::unordered_map<std::string,std::vector<std::string>> constraints={},bool forward=false)
        :params(std::move(p)),result(std::move(r)),genericParams(std::move(generic)),isExtern(external),isExport(exported),isForward(forward),genericConstraints(std::move(constraints)){}
};
struct RecordType { std::vector<RecordField> fields; std::size_t size=0,alignment=1; };
class TypeChecker { public: explicit TypeChecker(Diagnostics& diagnostics); bool check(const Program& program); private: void checkStmt(const StmtPtr& stmt); Type checkExpr(const ExprPtr& expr); Type checkExprAgainst(const ExprPtr& expr,const Type& expected); void pushScope(); void popScope(); void define(const std::string& name,Type type,bool isConst,Span span); std::optional<Type> resolve(const std::string& name) const; bool isConstSymbol(const std::string& name) const; Type resolveType(const std::string& name,Span span); std::optional<RecordField> resolveField(const Type& base,const std::string& member) const; Type substituteGeneric(const Type& type,const std::unordered_map<std::string,Type>& bindings) const; bool bindGeneric(const Type& pattern,const Type& actual,std::unordered_map<std::string,Type>& bindings) const; Diagnostics& diagnostics_; std::vector<std::unordered_map<std::string,Type>> scopes_; std::vector<std::unordered_map<std::string,bool>> constScopes_; std::unordered_map<std::string,FunctionType> functions_; std::unordered_map<std::string,RecordType> records_; std::unordered_set<std::string> activeGenericParams_; Type currentReturn_{}; bool insideFunction_=false; std::size_t loopDepth_=0; };

class BorrowChecker { public: bool check(const Program& program,Diagnostics& diagnostics) const; };
class SafetyAnnotator { public: bool check(const Program& program,Diagnostics& diagnostics,bool allowUnsafe=false) const; };
class Lowerer { public: NirProgram lower(const Program& program); private: void lowerStmt(NirFunction& fn,const StmtPtr& stmt); Reg lowerExpr(NirFunction& fn,const ExprPtr& expr); Reg lowerAddress(NirFunction& fn,const ExprPtr& expr); Reg emit(NirFunction& fn,NirInstruction instruction); std::vector<std::vector<std::size_t>> breakJumps_; std::vector<std::vector<std::size_t>> continueJumps_; };
enum class OptimizationLevel { O0, O1, O2, O3, Os, Oz };
class OptimizationPass { public: virtual ~OptimizationPass()=default; virtual const char* name() const=0; virtual bool run(NirFunction& function) const=0; };
class PassManager { public: explicit PassManager(OptimizationLevel level=OptimizationLevel::O1):level_(level){} void optimize(NirProgram& program) const; OptimizationLevel level() const { return level_; } private: OptimizationLevel level_; };
class Optimizer { public: void optimize(NirProgram& program) const; void optimize(NirProgram& program,OptimizationLevel level) const; private: void optimizeFunction(NirFunction& fn) const; };
struct CompileOptions { OptimizationLevel optimization=OptimizationLevel::O1; std::string target=NOQERI_DEFAULT_TARGET; bool allowUnsafe=false; };
struct CompileResult { Program ast; NirProgram nir; Diagnostics diagnostics; std::shared_ptr<ModuleGraph> modules; };
CompileResult compileSource(const std::string& source);
CompileResult compileSource(const std::string& source,const CompileOptions& options);
CompileResult compileFile(const std::filesystem::path& path);
CompileResult compileFile(const std::filesystem::path& path,const CompileOptions& options);
std::string readTextFile(const std::filesystem::path& path);

} // namespace noe
