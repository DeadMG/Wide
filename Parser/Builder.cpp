#include <Parser/Builder.h>

using namespace Wide;
using namespace AST;
     
Module* Builder::GetGlobalModule() {
    return GlobalModule;
}

Builder::Builder() 
{
    Wide::Memory::Arena arena;
    GlobalModule = arena.Allocate<Module>("global", nullptr);
    arenas.push(std::move(arena));
}

ThreadLocalBuilder::ThreadLocalBuilder(Builder& build)
    : b(&build) 
{
    // Re-use arena if possible; else stick with our new arena.
    build.arenas.try_pop(arena);
}

ThreadLocalBuilder::~ThreadLocalBuilder() {
    // Recycle this arena
    b->arenas.push(std::move(arena));
}

Using* ThreadLocalBuilder::CreateUsingDefinition(std::string name, Expression* val, Module* m) {
    auto p = arena.Allocate<Using>(std::move(name), val, m);
    auto ret = m->decls.insert(std::pair<const std::string, ModuleLevelDeclaration*>(p->name, p));
    if (!ret.second)
        throw std::runtime_error("Attempted to create a using, but there was already another declaration in that slot.");
    return p;
}

Module* ThreadLocalBuilder::CreateModule(std::string val, Module* m, Lexer::Range l) {
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

void ThreadLocalBuilder::CreateFunction(
    std::string name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range r,
    Type* p, 
    std::vector<FunctionArgument> args, 
    std::vector<VariableStatement*> caps) 
{
    if (name == "(")
        name = "()";
    if (p->Functions.find(name) != p->Functions.end())
        p->Functions[name]->functions.push_back(arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), p, std::move(caps)));
    else {
        auto pair = std::make_pair(
            arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), p, std::move(caps)),
            arena.Allocate<FunctionOverloadSet>(name, p)
        );
        p->Functions[name] = pair.second;
        pair.second->functions.push_back(pair.first);
    }
}

void ThreadLocalBuilder::CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* m, std::vector<FunctionArgument> args, std::vector<VariableStatement*> caps) {
    if (name == "(")
        throw std::runtime_error("operator() must be a type-scope function.");

    auto p = std::make_pair(
        arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), m, std::move(caps)), 
        arena.Allocate<FunctionOverloadSet>(name, m)
    );
    auto ret = m->decls.insert(std::make_pair(name, p.second));
    if (!ret.second) {
        if (auto ovrset = dynamic_cast<AST::FunctionOverloadSet*>(ret.first->second)) {
            ovrset->functions.push_back(p.first);
            return;
        } else
            throw std::runtime_error("Attempted to insert a function, but there was already another declaration in that slot that was not a function overload set.");
    }
    p.second->functions.push_back(p.first);
    return;
}

Type* ThreadLocalBuilder::CreateType(std::string name, DeclContext* higher, Lexer::Range loc) {
    auto ty = arena.Allocate<Type>(higher, name, loc);
    if (auto mod = dynamic_cast<AST::Module*>(higher)) {
        auto result = mod->decls.insert(std::make_pair(name, ty));
        if (!result.second)
            throw std::runtime_error("Attempted to insert a type into a module, but another type declaration already existed there.");
    }
    return ty;
}

IdentifierExpr* ThreadLocalBuilder::CreateIdentExpression(std::string name, Lexer::Range r) 
{ return arena.Allocate<IdentifierExpr>(std::move(name), r); }

StringExpr* ThreadLocalBuilder::CreateStringExpression(std::string val, Lexer::Range r) 
{ return arena.Allocate<StringExpr>(std::move(val), r); }

MemAccessExpr* ThreadLocalBuilder::CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
{ return arena.Allocate<MemAccessExpr>(std::move(mem), e, r); }

LeftShiftExpr* ThreadLocalBuilder::CreateLeftShiftExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<LeftShiftExpr>(lhs, rhs); }

FunctionCallExpr* ThreadLocalBuilder::CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<FunctionCallExpr>(e, std::move(args), r); }

Return* ThreadLocalBuilder::CreateReturn(Expression* expr, Lexer::Range r) 
{ return arena.Allocate<Return>(expr, r); }

Return* ThreadLocalBuilder::CreateReturn(Lexer::Range r) 
{ return arena.Allocate<Return>(r); }

VariableStatement* ThreadLocalBuilder::CreateVariableStatement(std::string name, Expression* value, Lexer::Range r) 
{ return arena.Allocate<VariableStatement>(std::move(name), value, r); }

VariableStatement* ThreadLocalBuilder::CreateVariableStatement(std::string name, Lexer::Range r) 
{ return CreateVariableStatement(std::move(name), nullptr, r); }

AssignmentExpr* ThreadLocalBuilder::CreateAssignmentExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<AssignmentExpr>(lhs, rhs); }

IntegerExpression* ThreadLocalBuilder::CreateIntegerExpression(std::string val, Lexer::Range r) 
{ return arena.Allocate<IntegerExpression>(std::move(val), r); }

RightShiftExpr* ThreadLocalBuilder::CreateRightShiftExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<RightShiftExpr>(lhs, rhs); }

IfStatement* ThreadLocalBuilder::CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc) 
{ return arena.Allocate<IfStatement>(cond, true_br, false_br, loc); }

IfStatement* ThreadLocalBuilder::CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc) 
{ return CreateIfStatement(cond, true_br, nullptr, loc); }

CompoundStatement* ThreadLocalBuilder::CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc) 
{ return arena.Allocate<CompoundStatement>(std::move(true_br), loc); }

EqCmpExpression* ThreadLocalBuilder::CreateEqCmpExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<EqCmpExpression>(lhs, rhs); }

NotEqCmpExpression* ThreadLocalBuilder::CreateNotEqCmpExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<NotEqCmpExpression>(lhs, rhs); }

MetaCallExpr* ThreadLocalBuilder::CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<MetaCallExpr>(e, std::move(args), r); }

WhileStatement* ThreadLocalBuilder::CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc) 
{ return arena.Allocate<WhileStatement>(body, cond, loc); }

ThisExpression* ThreadLocalBuilder::CreateThisExpression(Lexer::Range loc) 
{ return arena.Allocate<ThisExpression>(loc); }

Lambda* ThreadLocalBuilder::CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> caps) 
{ return arena.Allocate<Lambda>(std::move(body), std::move(args), loc, defaultref, std::move(caps)); }

NegateExpression* ThreadLocalBuilder::CreateNegateExpression(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<NegateExpression>(e, loc); }

DereferenceExpression* ThreadLocalBuilder::CreateDereferenceExpression(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<DereferenceExpression>(e, loc); }

PointerAccess* ThreadLocalBuilder::CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
{ return arena.Allocate<PointerAccess>(std::move(mem), e, r); }

OrExpression* ThreadLocalBuilder::CreateOrExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<OrExpression>(lhs, rhs); }

XorExpression* ThreadLocalBuilder::CreateXorExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<XorExpression>(lhs, rhs); }

AndExpression* ThreadLocalBuilder::CreateAndExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<AndExpression>(lhs, rhs); }

LTExpression* ThreadLocalBuilder::CreateLTExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<LTExpression>(lhs, rhs); }

LTEExpression* ThreadLocalBuilder::CreateLTEExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<LTEExpression>(lhs, rhs); }

GTExpression* ThreadLocalBuilder::CreateGTExpression(Expression* lhs, Expression* rhs)
{ return arena.Allocate<GTExpression>(lhs, rhs); }

GTEExpression* ThreadLocalBuilder::CreateGTEExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<GTEExpression>(lhs, rhs); }

Increment* ThreadLocalBuilder::CreatePrefixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, false); }

Increment* ThreadLocalBuilder::CreatePostfixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, true); }

Addition* ThreadLocalBuilder::CreateAdditionExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<Addition>(lhs, rhs); }

Multiply* ThreadLocalBuilder::CreateMultiplyExpression(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<Multiply>(lhs, rhs); }

AutoExpression* ThreadLocalBuilder::CreateAutoExpression(Lexer::Range loc)
{ return arena.Allocate<AutoExpression>(loc); }

Decrement* ThreadLocalBuilder::CreatePrefixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, false); }

Decrement* ThreadLocalBuilder::CreatePostfixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, true); }

AddressOfExpression* ThreadLocalBuilder::CreateAddressOf(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<AddressOfExpression>(ex, r); }

std::vector<Statement*> ThreadLocalBuilder::CreateStatementGroup() 
{ return std::vector<Statement*>(); }

std::vector<Expression*> ThreadLocalBuilder::CreateExpressionGroup() 
{ return std::vector<Expression*>(); }

std::vector<VariableStatement*> ThreadLocalBuilder::CreateCaptureGroup() 
{ return std::vector<VariableStatement*>(); }

std::vector<VariableStatement*> ThreadLocalBuilder::CreateInitializerGroup() 
{ return CreateCaptureGroup(); }

Type* ThreadLocalBuilder::CreateType(std::string name, Lexer::Range loc) { return CreateType(std::move(name), nullptr, loc); }
void ThreadLocalBuilder::SetTypeEndLocation(Lexer::Range loc, Type* t) { t->location = t->location + loc; }
void ThreadLocalBuilder::SetModuleEndLocation(Module* m, Lexer::Range loc) {}

std::vector<FunctionArgument> ThreadLocalBuilder::CreateFunctionArgumentGroup() { return std::vector<FunctionArgument>(); }

void ThreadLocalBuilder::AddTypeField(Type* t, VariableStatement* decl) { t->variables.push_back(decl); }
void ThreadLocalBuilder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Expression* expr)  {
    FunctionArgument arg;
    arg.name = name;
    arg.type = expr;
    args.push_back(arg);
}
void ThreadLocalBuilder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name) { return AddArgumentToFunctionGroup(args, std::move(name), nullptr); }
void ThreadLocalBuilder::AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement* cap) { return l.push_back(cap); }
void ThreadLocalBuilder::AddInitializerToGroup(std::vector<VariableStatement*>& l, VariableStatement* b) { return AddCaptureToGroup(l, b); }
void ThreadLocalBuilder::AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt) { return grp.push_back(stmt); }
void ThreadLocalBuilder::AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr) { return grp.push_back(expr); }

Lexer::Range ThreadLocalBuilder::GetLocation(Statement* s) {
    return s->location;
}

void ThreadLocalBuilder::Error(Wide::Lexer::Range, Parser::Error) {}