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
    auto ret = m->decls.insert(std::pair<const std::string, ModuleLevelDeclaration*>(p->identifier, p));
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

Function* Builder::CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* m, std::vector<Function::FunctionArgument> args) {
    auto p = ConcurrentUseArena(arenas, [&](Wide::Memory::Arena& arena) {
        return std::make_pair(
            arena.Allocate<Function>(name, std::move(body), std::move(prolog), r, std::move(args), m), 
            arena.Allocate<FunctionOverloadSet>(name, m));
    });
    auto ret = m->decls.insert(std::make_pair(name, p.second));
    if (!ret.second) {
        if (auto ovrset = dynamic_cast<AST::FunctionOverloadSet*>(ret.first->second)) {
            ovrset->functions.push_back(p.first);
            return p.first;
        } else
            throw std::runtime_error("Attempted to insert a function, but there was alreadya nother declaration in that slot that was not a function overload set.");
    }
    p.second->functions.push_back(p.first);
    return p.first;
}

std::vector<Function::FunctionArgument> Builder::CreateFunctionArgumentGroup() {
    return std::vector<Function::FunctionArgument>();
}

void Builder::AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>& args, std::string name) {
    Function::FunctionArgument arg;
    arg.name = name;
    arg.type = nullptr;
    args.push_back(arg);
}

void Builder::AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>& args, std::string name, Expression* t) {
    Function::FunctionArgument arg;
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