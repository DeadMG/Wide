#include <Wide/Parser/Builder.h>

using namespace Wide;
using namespace AST;
     
Builder::Builder(std::function<void(Lexer::Range, Parser::Error)> err, std::function<void(Lexer::Range, Parser::Warning)> warn)
    : GlobalModule("global", nullptr), error(std::move(err)), warning(std::move(warn))
{
}

Using* Builder::CreateUsingDefinition(std::string name, Expression* val, Module* m) {
    auto p = arena.Allocate<Using>(std::move(name), val, m);
    auto ret = m->decls.insert(std::pair<const std::string, ModuleLevelDeclaration*>(p->name, p));
    if (!ret.second)
        throw std::runtime_error("Attempted to create a using, but there was already another declaration in that slot.");
    return p;
}

Module* Builder::CreateModule(std::string val, Module* m, Lexer::Range l) {
    auto p = arena.Allocate<Module>(std::move(val), m);
    auto ret = m->decls.insert(std::pair<const std::string, ModuleLevelDeclaration*>(p->name, p));
    if (!ret.second) {
        if (auto mod = dynamic_cast<AST::Module*>(ret.first->second))
            return mod;
        else
            throw std::runtime_error("Attempted to create a module, but there was already another declaration in that slot that was not another module.");
    }
    return p;
}

void Builder::CreateFunction(
    std::string name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range r,
    Type* p, 
    std::vector<FunctionArgument> args, 
    std::vector<VariableStatement*> caps) 
{
    if (p->Functions.find(name) == p->Functions.end())
        p->Functions[name] = arena.Allocate<FunctionOverloadSet>(name, p);
    p->Functions[name]->functions.insert(arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), p, std::move(caps)));
}

void Builder::CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* m, std::vector<FunctionArgument> args, std::vector<VariableStatement*> caps) {
    auto func = arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), m, std::move(caps));
    if (m->decls.find(name) == m->decls.end()) {
        auto overset = arena.Allocate<FunctionOverloadSet>(name, m);
        overset->functions.insert(func);
        m->decls[name] = overset;
    } else
        if (auto overset = dynamic_cast<AST::FunctionOverloadSet*>(m->decls[name]))
            overset->functions.insert(func);
        else
            throw std::runtime_error("Attempted to insert a function, but there was already another declaration of that name in that module that was not a function overload set.");
}

void Builder::CreateOverloadedOperator(Wide::Lexer::TokenType name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* m, std::vector<FunctionArgument> args) {
    if (m->opcondecls.find(name) == m->opcondecls.end())
        m->opcondecls[name] = arena.Allocate<FunctionOverloadSet>("operator", m);
    m->opcondecls[name]->functions.insert(arena.Allocate<Function>("operator", std::move(body), std::move(prolog), r, std::move(args), m, std::vector<VariableStatement*>()));
}

Type* Builder::CreateType(std::string name, DeclContext* higher, Lexer::Range loc) {
    auto ty = arena.Allocate<Type>(higher, name, loc);
    if (auto mod = dynamic_cast<AST::Module*>(higher)) {
        auto result = mod->decls.insert(std::make_pair(name, ty));
        if (!result.second)
            throw std::runtime_error("Attempted to insert a type into a module, but another type declaration already existed there.");
    }
    return ty;
}

IdentifierExpr* Builder::CreateIdentExpression(std::string name, Lexer::Range r) 
{ return arena.Allocate<IdentifierExpr>(std::move(name), r); }

StringExpr* Builder::CreateStringExpression(std::string val, Lexer::Range r) 
{ return arena.Allocate<StringExpr>(std::move(val), r); }

MemAccessExpr* Builder::CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
{ return arena.Allocate<MemAccessExpr>(std::move(mem), e, r); }

BinaryExpression* Builder::CreateLeftShiftExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LeftShift); }

FunctionCallExpr* Builder::CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<FunctionCallExpr>(e, std::move(args), r); }

Return* Builder::CreateReturn(Expression* expr, Lexer::Range r) 
{ return arena.Allocate<Return>(expr, r); }

Return* Builder::CreateReturn(Lexer::Range r) 
{ return arena.Allocate<Return>(r); }

VariableStatement* Builder::CreateVariableStatement(std::string name, Expression* value, Lexer::Range r) 
{ return arena.Allocate<VariableStatement>(std::move(name), value, r); }

VariableStatement* Builder::CreateVariableStatement(std::string name, Lexer::Range r) 
{ return CreateVariableStatement(std::move(name), nullptr, r); }

BinaryExpression* Builder::CreateAssignmentExpression(Expression* lhs, Expression* rhs, Lexer::TokenType type) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, type); }

IntegerExpression* Builder::CreateIntegerExpression(std::string val, Lexer::Range r) 
{ return arena.Allocate<IntegerExpression>(std::move(val), r); }

BinaryExpression* Builder::CreateRightShiftExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::RightShift); }

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc) 
{ return arena.Allocate<IfStatement>(cond, true_br, false_br, loc); }

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc) 
{ return CreateIfStatement(cond, true_br, nullptr, loc); }

CompoundStatement* Builder::CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc) 
{ return arena.Allocate<CompoundStatement>(std::move(true_br), loc); }

BinaryExpression* Builder::CreateEqCmpExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::EqCmp); }

BinaryExpression* Builder::CreateNotEqCmpExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::NotEqCmp); }

MetaCallExpr* Builder::CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<MetaCallExpr>(e, std::move(args), r); }

WhileStatement* Builder::CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc) 
{ return arena.Allocate<WhileStatement>(body, cond, loc); }

ThisExpression* Builder::CreateThisExpression(Lexer::Range loc) 
{ return arena.Allocate<ThisExpression>(loc); }

Lambda* Builder::CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> caps) 
{ return arena.Allocate<Lambda>(std::move(body), std::move(args), loc, defaultref, std::move(caps)); }

NegateExpression* Builder::CreateNegateExpression(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<NegateExpression>(e, loc); }

DereferenceExpression* Builder::CreateDereferenceExpression(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<DereferenceExpression>(e, loc); }

PointerAccess* Builder::CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
{ return arena.Allocate<PointerAccess>(std::move(mem), e, r); }

BinaryExpression* Builder::CreateOrExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Or); }

BinaryExpression* Builder::CreateXorExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Xor); }

BinaryExpression* Builder::CreateAndExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::And); }

BinaryExpression* Builder::CreateLTExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LT); }

BinaryExpression* Builder::CreateLTEExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LTE); }

BinaryExpression* Builder::CreateGTExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::GT); }

BinaryExpression* Builder::CreateGTEExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::GTE); }

Increment* Builder::CreatePrefixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, false); }

Increment* Builder::CreatePostfixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, true); }

BinaryExpression* Builder::CreateAdditionExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Plus); }

BinaryExpression* Builder::CreateMultiplyExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Dereference); }

AutoExpression* Builder::CreateAutoExpression(Lexer::Range loc)
{ return arena.Allocate<AutoExpression>(loc); }

Decrement* Builder::CreatePrefixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, false); }

Decrement* Builder::CreatePostfixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, true); }

AddressOfExpression* Builder::CreateAddressOf(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<AddressOfExpression>(ex, r); }

std::vector<Statement*> Builder::CreateStatementGroup() 
{ return std::vector<Statement*>(); }

std::vector<Expression*> Builder::CreateExpressionGroup() 
{ return std::vector<Expression*>(); }

std::vector<VariableStatement*> Builder::CreateCaptureGroup() 
{ return std::vector<VariableStatement*>(); }

std::vector<VariableStatement*> Builder::CreateInitializerGroup() 
{ return CreateCaptureGroup(); }

Type* Builder::CreateType(std::string name, Lexer::Range loc) { return CreateType(std::move(name), nullptr, loc); }
void Builder::SetTypeEndLocation(Lexer::Range loc, Type* t) { t->location = t->location + loc; }
void Builder::SetModuleEndLocation(Module* m, Lexer::Range loc) {}

std::vector<FunctionArgument> Builder::CreateFunctionArgumentGroup() { return std::vector<FunctionArgument>(); }

void Builder::AddTypeField(Type* t, VariableStatement* decl) { t->variables.push_back(decl); }
void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Expression* expr)  {
    FunctionArgument arg;
    arg.name = name;
    arg.type = expr;
    args.push_back(arg);
}
void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name) { return AddArgumentToFunctionGroup(args, std::move(name), nullptr); }
void Builder::AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement* cap) { return l.push_back(cap); }
void Builder::AddInitializerToGroup(std::vector<VariableStatement*>& l, VariableStatement* b) { return AddCaptureToGroup(l, b); }
void Builder::AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt) { return grp.push_back(stmt); }
void Builder::AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr) { return grp.push_back(expr); }

Lexer::Range Builder::GetLocation(Statement* s) {
    return s->location;
}

void Builder::Error(Wide::Lexer::Range r, Parser::Error e) {
    error(r, e);
}

BinaryExpression* Builder::CreateDivisionExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Divide); }
BinaryExpression* Builder::CreateModulusExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Modulo); }
BinaryExpression* Builder::CreateSubtractionExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Minus); }

void Builder::Warning(Wide::Lexer::Range where, Parser::Warning what) {
    return warning(where, what);
}

ErrorExpression* Builder::CreateErrorExpression(Wide::Lexer::Range where) {
    return arena.Allocate<ErrorExpression>(where);
}