#include "Module.h"
#include "../Parser/AST.h"
#include "Analyzer.h"
#include "Function.h"
#include "OverloadSet.h"
#include "ConstructorType.h"
#include "UserDefinedType.h"

#pragma warning(push, 0)

#include <clang/AST/Type.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Module::Module(AST::Module* p) 
    : m(p) {}

void Module::AddSpecialMember(std::string name, Expression t){
    SpecialMembers[std::move(name)] = t;
}
Expression Module::AccessMember(Expression val, std::string name, Analyzer& a) {
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls[name];
        if (auto moddecl = dynamic_cast<AST::Module*>(decl)) {
            Expression r;
            r.t = a.GetWideModule(moddecl);
            r.Expr = nullptr;
            return r;
        }
        if (auto usedecl = dynamic_cast<AST::Using*>(decl)) {
            return a.AnalyzeExpression(this, usedecl->expr);
        }
        if (auto overdecl = dynamic_cast<AST::FunctionOverloadSet*>(decl)) {
            return a.GetOverloadSet(overdecl)->BuildValueConstruction(std::vector<Expression>(), a);          
        }
        if (auto tydecl = dynamic_cast<AST::Type*>(decl)) {
            Expression r;
            r.t = a.GetConstructorType(a.GetUDT(tydecl));
            r.Expr = nullptr;
            return r;
        }
        throw std::runtime_error("Attempted to access a member of a Wide module that was not either another module, or a function.");
    }
    if (SpecialMembers.find(name) != SpecialMembers.end()) {
        return SpecialMembers[name];
    }
    if (m->higher)
        return a.GetDeclContext(m->higher)->AccessMember(val, name, a);
    throw std::runtime_error("Attempted to access a member of a Wide module that did not exist.");
}

AST::DeclContext* Module::GetDeclContext() {
    return m->higher;
}