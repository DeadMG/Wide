#include "Module.h"
#include "../Parser/AST.h"
#include "LvalueType.h"
#include "Analyzer.h"
#include "Function.h"
#include "ClangNamespace.h"
#include "OverloadSet.h"
#include "ConstructorType.h"
#include "UserDefinedType.h"
#include "ClangTemplateClass.h"
#include "../Codegen/Generator.h"

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
            auto expr = a.AnalyzeExpression(this, usedecl->expr);
            if (auto conty = dynamic_cast<ConstructorType*>(expr.t->Decay())) {
                return conty->BuildValueConstruction(std::vector<Expression>(), a);
            }
            if (auto fun = dynamic_cast<OverloadSet*>(expr.t->Decay()))
                return expr;
            if (auto temp = dynamic_cast<ClangTemplateClass*>(expr.t))
                return expr;
            if (auto lval = dynamic_cast<LvalueType*>(expr.t))
                return expr;
            if (auto nam = dynamic_cast<ClangNamespace*>(expr.t))
                return expr;
            if (auto mod = dynamic_cast<Module*>(expr.t))
                return expr;
            throw std::runtime_error("Attempted to using something that was not a type, template, module, or function");
        }
        if (auto overdecl = dynamic_cast<AST::FunctionOverloadSet*>(decl)) {
            return a.GetOverloadSet(overdecl)->BuildValueConstruction(std::vector<Expression>(), a);          
        }
        if (auto tydecl = dynamic_cast<AST::Type*>(decl)) {
            Expression r;
            r.t = a.GetConstructorType(a.GetUDT(tydecl, this));
            r.Expr = a.gen->CreateLoad(a.gen->CreateVariable(a.GetConstructorType(a.GetUDT(tydecl, this))->GetLLVMType(a)));
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
    return m;
}