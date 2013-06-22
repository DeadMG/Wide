#include "Builder.h"

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

Module* ThreadLocalBuilder::CreateModule(std::string val, Module* m) {
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