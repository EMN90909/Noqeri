#include "noe.hpp"
#include <stdexcept>

namespace noe {
namespace {
struct ParseError:std::runtime_error{using std::runtime_error::runtime_error;};

std::string decodeStringLiteral(const std::string& lexeme){
    if(lexeme.size()<2)return {};
    std::string out;
    out.reserve(lexeme.size()-2);
    for(std::size_t i=1;i+1<lexeme.size();++i){
        char c=lexeme[i];
        if(c!='\\'||i+2>=lexeme.size()){out+=c;continue;}
        char escaped=lexeme[++i];
        switch(escaped){
            case '\\':out+='\\';break;
            case '"':out+='"';break;
            case 'n':out+='\n';break;
            case 'r':out+='\r';break;
            case 't':out+='\t';break;
            case '0':out+='\0';break;
            default:out+=escaped;break;
        }
    }
    return out;
}
}

Parser::Parser(std::vector<Token> tokens,Diagnostics& diagnostics):tokens_(std::move(tokens)),diagnostics_(diagnostics){}

Program Parser::parse(){
    Program p;
    while(!check(TokenKind::Eof)){
        try{p.statements.push_back(declaration());}
        catch(const ParseError&){synchronize();}
    }
    return p;
}

StmtPtr Parser::declaration(){
    bool isExport=false,isExtern=false;
    if(match({TokenKind::Export})) isExport=true;
    if(match({TokenKind::Extern})) isExtern=true;
    if(isExport && match({TokenKind::Extern})) isExtern=true;
    if(isExtern && match({TokenKind::Export})) isExport=true;
    if(match({TokenKind::Function})) return functionDeclaration(isExtern,isExport);
    if(isExport||isExtern){diagnostics_.error("NOE-P2010",previous().span,"export/extern currently apply to function declarations");throw ParseError("parse");}
    if(match({TokenKind::Module}))return moduleDeclaration();
    if(match({TokenKind::Import}))return importDeclaration();
    if(match({TokenKind::Record}))return recordDeclaration();
    if(match({TokenKind::Let}))return letDeclaration(false);
    if(match({TokenKind::Const}))return letDeclaration(true);
    return statement();
}

std::vector<std::string> Parser::parseGenericParams(){
    std::vector<std::string> out;
    if(!match({TokenKind::Less}))return out;
    do{out.push_back(consume(TokenKind::Identifier,"expected generic type parameter").lexeme);}while(match({TokenKind::Comma}));
    consume(TokenKind::Greater,"expected '>' after generic parameters");
    return out;
}

std::string Parser::parseTypeName(){
    if(match({TokenKind::Star})){
        bool vol=match({TokenKind::Volatile});
        return std::string("*")+(vol?"volatile ":"")+parseTypeName();
    }
    if(match({TokenKind::LBracket})){
        if(match({TokenKind::RBracket}))return "[]"+parseTypeName();
        std::string element=parseTypeName();
        consume(TokenKind::Semicolon,"expected ';' between array element type and length");
        const Token& count=consume(TokenKind::Integer,"expected fixed array length");
        consume(TokenKind::RBracket,"expected ']' after fixed array type");
        return "["+element+";"+count.lexeme+"]";
    }
    const Token& base=consume(TokenKind::Identifier,"expected type name");
    return base.lexeme;
}

StmtPtr Parser::moduleDeclaration(){
    const Token& first=consume(TokenKind::Identifier,"expected module name");
    std::string name=first.lexeme;
    while(match({TokenKind::Dot}))name+="."+consume(TokenKind::Identifier,"expected module name segment").lexeme;
    match({TokenKind::Semicolon});
    auto m=std::make_shared<ModuleStmt>();m->span=first.span;m->name=std::move(name);return m;
}

StmtPtr Parser::importDeclaration(){
    const Token& path=consume(TokenKind::String,"expected quoted import path, for example import \"math.noqeri\"");
    auto i=std::make_shared<ImportStmt>();i->span=path.span;i->path=decodeStringLiteral(path.lexeme);
    match({TokenKind::Semicolon});return i;
}

StmtPtr Parser::recordDeclaration(){
    const Token& name=consume(TokenKind::Identifier,"expected record name");
    consume(TokenKind::LBrace,"expected '{' after record name");
    auto r=std::make_shared<RecordStmt>();r->name=name.lexeme;r->span=name.span;
    while(!check(TokenKind::RBrace)&&!check(TokenKind::Eof)){
        const Token& field=consume(TokenKind::Identifier,"expected field name");
        consume(TokenKind::Colon,"expected ':' after field name");
        r->fields.push_back(RecordField{field.lexeme,parseTypeName(),field.span});
        match({TokenKind::Comma,TokenKind::Semicolon});
    }
    consume(TokenKind::RBrace,"expected '}' after record fields");
    match({TokenKind::Semicolon});
    return r;
}

StmtPtr Parser::functionDeclaration(bool isExtern,bool isExport){
    const Token& name=consume(TokenKind::Identifier,"expected function name");
    auto genericParams=parseGenericParams();
    consume(TokenKind::LParen,"expected '(' after function name");
    std::vector<Parameter> params;
    if(!check(TokenKind::RParen)){
        do{
            const Token& p=consume(TokenKind::Identifier,"expected parameter name");
            std::optional<std::string> type;
            if(match({TokenKind::Colon}))type=parseTypeName();
            params.push_back({p.lexeme,type,p.span});
        }while(match({TokenKind::Comma}));
    }
    consume(TokenKind::RParen,"expected ')' after parameters");
    std::optional<std::string> ret;
    if(match({TokenKind::Colon}))ret=parseTypeName();
    auto fn=std::make_shared<FunctionStmt>();
    fn->span=name.span;fn->name=name.lexeme;fn->genericParams=std::move(genericParams);fn->params=std::move(params);fn->returnType=ret;fn->isExtern=isExtern;fn->isExport=isExport;
    if(isExtern){match({TokenKind::Semicolon});fn->body=std::make_shared<BlockStmt>();return fn;}
    consume(TokenKind::LBrace,"expected '{' before function body");
    fn->body=block();
    return fn;
}

StmtPtr Parser::letDeclaration(bool isConst){
    const Token& name=consume(TokenKind::Identifier,"expected variable name");
    std::optional<std::string> annotation;
    if(match({TokenKind::Colon}))annotation=parseTypeName();
    auto s=std::make_shared<LetStmt>();s->span=name.span;s->isConst=isConst;s->name=name.lexeme;s->annotation=annotation;
    if(match({TokenKind::Equal})){
        s->initializer=expression();
    }else{
        if(!annotation){
            diagnostics_.error("NOE-P2011",name.span,"uninitialized variable requires an explicit type annotation","write 'let name: Type' or provide '= value'");
            throw ParseError("parse");
        }
        if(isConst){
            diagnostics_.error("NOE-P2012",name.span,"const declarations require an initializer","use 'let' for a zero-initialized typed local");
            throw ParseError("parse");
        }
        s->zeroInitialized=true;
    }
    match({TokenKind::Semicolon});return s;
}

StmtPtr Parser::statement(){
    if(match({TokenKind::If}))return ifStatement();
    if(match({TokenKind::While}))return whileStatement();
    if(match({TokenKind::Repeat}))return repeatStatement();
    if(match({TokenKind::Return}))return returnStatement();
    if(match({TokenKind::Throw}))return throwStatement();
    if(match({TokenKind::Unsafe})){
        Token start=previous();
        consume(TokenKind::LBrace,"expected '{' after unsafe");
        auto body=block();
        body->span=start.span;
        body->isUnsafe=true;
        return body;
    }
    if(match({TokenKind::LBrace}))return block();
    return expressionStatement();
}
StmtPtr Parser::ifStatement(){Token start=previous();ExprPtr cond=expression();auto s=std::make_shared<IfStmt>();s->span=start.span;s->condition=cond;s->thenBranch=statement();if(match({TokenKind::Else}))s->elseBranch=statement();return s;}
StmtPtr Parser::whileStatement(){Token start=previous();ExprPtr cond=expression();auto s=std::make_shared<WhileStmt>();s->span=start.span;s->condition=cond;s->body=statement();return s;}
StmtPtr Parser::repeatStatement(){
    Token start=previous();
    auto count=expression();
    consume(TokenKind::LBrace,"expected '{' after repeat count");
    auto userBody=block();

    const std::string suffix=std::to_string(start.span.source)+"_"+std::to_string(start.span.start);
    const std::string limitName="__nq_repeat_limit_"+suffix;
    const std::string indexName="__nq_repeat_index_"+suffix;

    auto countCast=std::make_shared<CastExpr>();countCast->span=start.span;countCast->value=count;countCast->typeName="int";
    auto limit=std::make_shared<LetStmt>();limit->span=start.span;limit->name=limitName;limit->annotation=std::string("int");limit->initializer=countCast;

    auto zero=std::make_shared<LiteralExpr>();zero->span=start.span;zero->value=static_cast<std::int64_t>(0);
    auto index=std::make_shared<LetStmt>();index->span=start.span;index->name=indexName;index->annotation=std::string("int");index->initializer=zero;

    auto indexCond=std::make_shared<NameExpr>();indexCond->span=start.span;indexCond->name=indexName;
    auto limitCond=std::make_shared<NameExpr>();limitCond->span=start.span;limitCond->name=limitName;
    auto condition=std::make_shared<BinaryExpr>();condition->span=start.span;condition->left=indexCond;condition->op=TokenKind::Less;condition->right=limitCond;

    auto indexLeft=std::make_shared<NameExpr>();indexLeft->span=start.span;indexLeft->name=indexName;
    auto indexRead=std::make_shared<NameExpr>();indexRead->span=start.span;indexRead->name=indexName;
    auto one=std::make_shared<LiteralExpr>();one->span=start.span;one->value=static_cast<std::int64_t>(1);
    auto add=std::make_shared<BinaryExpr>();add->span=start.span;add->left=indexRead;add->op=TokenKind::Plus;add->right=one;
    auto assign=std::make_shared<BinaryExpr>();assign->span=start.span;assign->left=indexLeft;assign->op=TokenKind::Equal;assign->right=add;
    auto increment=std::make_shared<ExprStmt>();increment->span=start.span;increment->expr=assign;

    auto loopBody=std::make_shared<BlockStmt>();loopBody->span=userBody->span;loopBody->statements=userBody->statements;loopBody->statements.push_back(increment);
    auto loop=std::make_shared<WhileStmt>();loop->span=start.span;loop->condition=condition;loop->body=loopBody;

    auto outer=std::make_shared<BlockStmt>();outer->span=start.span;outer->statements.push_back(limit);outer->statements.push_back(index);outer->statements.push_back(loop);
    return outer;
}
StmtPtr Parser::returnStatement(){Token start=previous();auto s=std::make_shared<ReturnStmt>();s->span=start.span;if(!check(TokenKind::Semicolon)&&!check(TokenKind::RBrace)&&!check(TokenKind::Eof))s->value=expression();match({TokenKind::Semicolon});return s;}
StmtPtr Parser::throwStatement(){Token start=previous();auto s=std::make_shared<ThrowStmt>();s->span=start.span;s->value=expression();match({TokenKind::Semicolon});return s;}
std::shared_ptr<BlockStmt> Parser::block(){auto b=std::make_shared<BlockStmt>();b->span=previous().span;while(!check(TokenKind::RBrace)&&!check(TokenKind::Eof)){try{b->statements.push_back(declaration());}catch(const ParseError&){synchronize();}}consume(TokenKind::RBrace,"expected '}' after block");return b;}
StmtPtr Parser::expressionStatement(){auto s=std::make_shared<ExprStmt>();s->expr=expression();s->span=s->expr->span;match({TokenKind::Semicolon});return s;}

ExprPtr Parser::expression(){return assignment();}
ExprPtr Parser::assignment(){
    auto left=logicalOr();
    if(match({TokenKind::Equal})){
        Token eq=previous();auto value=assignment();
        bool valid=std::dynamic_pointer_cast<NameExpr>(left)||std::dynamic_pointer_cast<IndexExpr>(left)||std::dynamic_pointer_cast<MemberExpr>(left);
        if(auto u=std::dynamic_pointer_cast<UnaryExpr>(left))valid=u->op==TokenKind::Star;
        if(!valid){diagnostics_.error("NOE-P2004",eq.span,"invalid assignment target");return left;}
        auto b=std::make_shared<BinaryExpr>();b->span=eq.span;b->left=left;b->op=TokenKind::Equal;b->right=value;return b;
    }
    return left;
}
static ExprPtr makeBinary(ExprPtr l,TokenKind op,ExprPtr r,Span span){auto b=std::make_shared<BinaryExpr>();b->left=std::move(l);b->op=op;b->right=std::move(r);b->span=span;return b;}
ExprPtr Parser::logicalOr(){auto e=logicalAnd();while(match({TokenKind::OrOr})){auto o=previous();e=makeBinary(e,o.kind,logicalAnd(),o.span);}return e;}
ExprPtr Parser::logicalAnd(){auto e=equality();while(match({TokenKind::AndAnd})){auto o=previous();e=makeBinary(e,o.kind,equality(),o.span);}return e;}
ExprPtr Parser::equality(){auto e=comparison();while(match({TokenKind::EqualEqual,TokenKind::BangEqual})){auto o=previous();e=makeBinary(e,o.kind,comparison(),o.span);}return e;}
ExprPtr Parser::comparison(){auto e=term();while(match({TokenKind::Less,TokenKind::LessEqual,TokenKind::Greater,TokenKind::GreaterEqual})){auto o=previous();e=makeBinary(e,o.kind,term(),o.span);}return e;}
ExprPtr Parser::term(){auto e=factor();while(match({TokenKind::Plus,TokenKind::Minus})){auto o=previous();e=makeBinary(e,o.kind,factor(),o.span);}return e;}
ExprPtr Parser::factor(){auto e=unary();while(match({TokenKind::Star,TokenKind::Slash,TokenKind::Percent})){auto o=previous();e=makeBinary(e,o.kind,unary(),o.span);}return e;}
ExprPtr Parser::unary(){if(match({TokenKind::Bang,TokenKind::Minus,TokenKind::Plus,TokenKind::Star,TokenKind::Ampersand,TokenKind::Try})){auto op=previous();auto u=std::make_shared<UnaryExpr>();u->op=op.kind;u->operand=unary();u->span=op.span;return u;}return cast();}
ExprPtr Parser::cast(){auto e=call();while(match({TokenKind::As})){auto c=std::make_shared<CastExpr>();c->span=previous().span;c->value=e;c->typeName=parseTypeName();e=c;}return e;}
ExprPtr Parser::call(){
    auto e=primary();
    for(;;){
        if(match({TokenKind::LParen})){auto c=std::make_shared<CallExpr>();c->callee=e;c->span=previous().span;if(!check(TokenKind::RParen))do{c->args.push_back(expression());}while(match({TokenKind::Comma}));consume(TokenKind::RParen,"expected ')' after arguments");e=c;continue;}
        if(match({TokenKind::LBracket})){auto x=std::make_shared<IndexExpr>();x->object=e;x->index=expression();x->span=previous().span;consume(TokenKind::RBracket,"expected ']' after index");e=x;continue;}
        if(match({TokenKind::Dot})){const auto& m=consume(TokenKind::Identifier,"expected field name after '.'");auto x=std::make_shared<MemberExpr>();x->object=e;x->member=m.lexeme;x->span=m.span;e=x;continue;}
        break;
    }
    return e;
}
ExprPtr Parser::primary(){
    if(match({TokenKind::Integer})){auto x=std::make_shared<LiteralExpr>();x->span=previous().span;x->value=static_cast<std::int64_t>(std::stoull(previous().lexeme,nullptr,0));return x;}
    if(match({TokenKind::Float})){auto x=std::make_shared<LiteralExpr>();x->span=previous().span;x->value=std::stod(previous().lexeme);return x;}
    if(match({TokenKind::True,TokenKind::False})){auto x=std::make_shared<LiteralExpr>();x->span=previous().span;x->value=previous().kind==TokenKind::True;return x;}
    if(match({TokenKind::Null})){auto x=std::make_shared<LiteralExpr>();x->span=previous().span;x->value=std::monostate{};return x;}
    if(match({TokenKind::String})){auto x=std::make_shared<LiteralExpr>();x->span=previous().span;x->value=decodeStringLiteral(previous().lexeme);return x;}
    if(match({TokenKind::Identifier})){auto x=std::make_shared<NameExpr>();x->span=previous().span;x->name=previous().lexeme;return x;}
    if(match({TokenKind::LBracket})){
        auto a=std::make_shared<ArrayExpr>();a->span=previous().span;
        if(!check(TokenKind::RBracket))do{a->elements.push_back(expression());}while(match({TokenKind::Comma}));
        consume(TokenKind::RBracket,"expected ']' after array literal");return a;
    }
    if(match({TokenKind::LParen})){auto e=expression();consume(TokenKind::RParen,"expected ')' after expression");return e;}
    diagnostics_.error("NOE-P2001",peek().span,"expected expression");throw ParseError("parse");
}

bool Parser::match(std::initializer_list<TokenKind> kinds){for(auto k:kinds)if(check(k)){advance();return true;}return false;}
bool Parser::check(TokenKind kind)const{return peek().kind==kind;}
const Token& Parser::advance(){if(!check(TokenKind::Eof))++current_;return previous();}
const Token& Parser::previous()const{return tokens_[current_-1];}
const Token& Parser::peek()const{return tokens_[current_];}
const Token& Parser::consume(TokenKind kind,const std::string& message){if(check(kind))return advance();diagnostics_.error("NOE-P2000",peek().span,message);throw ParseError("parse");}
void Parser::synchronize(){if(!check(TokenKind::Eof))advance();while(!check(TokenKind::Eof)){if(previous().kind==TokenKind::Semicolon)return;switch(peek().kind){case TokenKind::Function:case TokenKind::Record:case TokenKind::Module:case TokenKind::Import:case TokenKind::Extern:case TokenKind::Export:case TokenKind::Let:case TokenKind::Const:case TokenKind::If:case TokenKind::While:case TokenKind::Repeat:case TokenKind::Return:case TokenKind::Throw:case TokenKind::Unsafe:return;default:break;}advance();}}
} // namespace noe
