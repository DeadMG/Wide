#include "Builder.h"

using namespace Wide;
using namespace AST;

template<typename F, typename T> auto ConcurrentUseArena(T&& t, F&& f) -> decltype(f(std::declval<Wide::Memory::Arena&>())) {
    Wide::Memory::Arena a;
    t.try_pop(a);
    struct last {
        Wide::Memory::Arena* a;
        typename std::decay<T>::type* t;
        ~last() {
            t->push(std::move(*a));
        }
    };
    last l;
    l.a = &a;
    l.t = &t;
    return f(a);
}
     
Module* Builder::GetGlobalModule() {
    return GlobalModule;
}

Builder::Builder() 
{
    ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        GlobalModule = arena.Allocate<Module>("global", nullptr);
    });
}

Return* Builder::CreateReturn(Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Return>(r);
    });
}

Return* Builder::CreateReturn(Expression* e, Lexer::Range r) {    
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Return>(e, r);
    });
}

VariableStatement* Builder::CreateVariableStatement(std::string name, Expression* value, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<VariableStatement>(std::move(name), value, r);
    });
}

AssignmentExpr* Builder::CreateAssignmentExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<AssignmentExpr>(lhs, rhs);
    });
}

IntegerExpression* Builder::CreateIntegerExpression(std::string val, Lexer::Range r) {    
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<IntegerExpression>(std::move(val), r);
    });
}

IdentifierExpr* Builder::CreateIdentExpression(std::string name, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<IdentifierExpr>(std::move(name), r);
    });
}

std::vector<Expression*> Builder::CreateExpressionGroup() { return std::vector<Expression*>(); }
std::vector<Statement*> Builder::CreateStatementGroup() { return std::vector<Statement*>(); }

StringExpr* Builder::CreateStringExpression(std::string val, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<StringExpr>(std::move(val), r);
    });
}

MemAccessExpr* Builder::CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<MemAccessExpr>(std::move(mem), e, r);
    });
}

LeftShiftExpr* Builder::CreateLeftShiftExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<LeftShiftExpr>(lhs, rhs);
    });
}
FunctionCallExpr* Builder::CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) {    
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<FunctionCallExpr>(e, std::move(args), r);
    });
}

RightShiftExpr* Builder::CreateRightShiftExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<RightShiftExpr>(lhs, rhs);
    });
}

Using* Builder::CreateUsingDefinition(std::string name, Expression* val, Module* m) {
    auto p = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Using>(std::move(name), val, m);
    });
    auto ret = m->decls.insert(std::pair<const std::string, ModuleLevelDeclaration*>(p->name, p));
    if (!ret.second)
        throw std::runtime_error("Attempted to create a using, but there was already another declaration in that slot.");
    return p;
}

Module* Builder::CreateModule(std::string val, Module* m) {
    auto p = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Module>(std::move(val), m);
    });
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
    if (name == "(")
        name = "()";
    if (p->Functions.find(name) != p->Functions.end())
        p->Functions[name]->functions.push_back(ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) -> AST::Function* {
            return arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), p, std::move(caps));
        }));
    else {
        auto pair = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
            return std::make_pair(
                arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), p, std::move(caps)),
                arena.Allocate<FunctionOverloadSet>(name, p));
        });
        p->Functions[name] = pair.second;
        pair.second->functions.push_back(pair.first);
    }
}

void Builder::CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* m, std::vector<FunctionArgument> args, std::vector<VariableStatement*> caps) {
    if (name == "(")
        throw std::runtime_error("operator() must be a type-scope function.");

    auto p = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return std::make_pair(
            arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), m, std::move(caps)), 
            arena.Allocate<FunctionOverloadSet>(name, m));
    });
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

std::vector<FunctionArgument> Builder::CreateFunctionArgumentGroup() {
    return std::vector<FunctionArgument>();
}

void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name) {
    FunctionArgument arg;
    arg.name = name;
    arg.type = nullptr;
    args.push_back(arg);
}

void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Expression* t) {
    FunctionArgument arg;
    arg.name = name;
    arg.type = t;
    args.push_back(arg);
}

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<IfStatement>(cond, true_br, false_br, loc);
    });
}

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc) { 
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<IfStatement>(cond, true_br, nullptr, loc);
    });
}

CompoundStatement* Builder::CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc) { 
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<CompoundStatement>(std::move(true_br), loc);
    });
}

EqCmpExpression* Builder::CreateEqCmpExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<EqCmpExpression>(lhs, rhs);
    });
}
NotEqCmpExpression* Builder::CreateNotEqCmpExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<NotEqCmpExpression>(lhs, rhs);
    });
}

MetaCallExpr* Builder::CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) {   
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<MetaCallExpr>(e, std::move(args), r);
    });
}

WhileStatement* Builder::CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<WhileStatement>(body, cond, loc);
    });
}

OrExpression* Builder::CreateOrExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<OrExpression>(lhs, rhs);
    });
}

XorExpression* Builder::CreateXorExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<XorExpression>(lhs, rhs);
    });
}

AndExpression* Builder::CreateAndExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<AndExpression>(lhs, rhs);
    });
}

LTExpression* Builder::CreateLTExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<LTExpression>(lhs, rhs);
    });
}

GTExpression* Builder::CreateGTExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<GTExpression>(lhs, rhs);
    });
}

GTEExpression* Builder::CreateGTEExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<GTEExpression>(lhs, rhs);
    });
}

LTEExpression* Builder::CreateLTEExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<LTEExpression>(lhs, rhs);
    });
}

Type* Builder::CreateType(std::string name, DeclContext* higher) {
    auto ty = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Type>(higher, name);
    });
    if (auto mod = dynamic_cast<AST::Module*>(higher)) {
        auto result = mod->decls.insert(std::make_pair(name, ty));
        if (!result.second)
            throw std::runtime_error("Attempted to insert a type into a module, but another type declaration already existed there.");
    }
    return ty;
}

ThisExpression* Builder::CreateThisExpression(Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<ThisExpression>(loc);
    });
}

void Builder::AddTypeField(Type* t, VariableStatement* decl) {
    t->variables.push_back(decl);
}

Lambda* Builder::CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> captures) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Lambda>(std::move(body), std::move(args), loc, defaultref, std::move(captures));
    });
}

void Builder::AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement* b) {
    l.push_back(b);
}

std::vector<VariableStatement*> Builder::CreateCaptureGroup() {
    return std::vector<VariableStatement*>();
}

DereferenceExpression* Builder::CreateDereferenceExpression(Expression* e, Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<DereferenceExpression>(e, loc);
    });
}

NegateExpression* Builder::CreateNegateExpression(Expression* e, Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<NegateExpression>(e, loc);
    });
}

Increment* Builder::CreatePrefixIncrement(Expression* ex, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Increment>(ex, r, false);
    });
}
Increment* Builder::CreatePostfixIncrement(Expression* ex, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Increment>(ex, r, true);
    });
}
Decrement* Builder::CreatePrefixDecrement(Expression* ex, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Decrement>(ex, r, false);
    });
}
Decrement* Builder::CreatePostfixDecrement(Expression* ex, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Decrement>(ex, r, true);
    });
}
Addition* Builder::CreateAdditionExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Addition>(lhs, rhs);
    });
}
Multiply* Builder::CreateMultiplyExpression(Expression* lhs, Expression* rhs) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<Multiply>(lhs, rhs);
    });
}

PointerAccess* Builder::CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<PointerAccess>(std::move(mem), e, r);
    });
}

AutoExpression* Builder::CreateAutoExpression(Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<AutoExpression>(loc);
    });
}

AddressOfExpression* Builder::CreateAddressOf(Expression* e, Lexer::Range loc) {
    return ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return arena.Allocate<AddressOfExpression>(e, loc);
    });
}