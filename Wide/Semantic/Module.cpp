#include <Wide/Semantic/Module.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Codegen/Generator.h>

using namespace Wide;
using namespace Semantic;

Module::Module(const AST::Module* p, Module* higher) 
    : m(p), context(higher) {}

void Module::AddSpecialMember(std::string name, ConcreteExpression t){
    SpecialMembers[std::move(name)] = t;
}
OverloadSet* Module::AccessMember(ConcreteExpression val, Wide::Lexer::TokenType ty, Analyzer& a, Lexer::Range where) {
    if (m->opcondecls.find(ty) != m->opcondecls.end())
        return a.GetOverloadSet(m->opcondecls.find(ty)->second, this);
    return a.GetOverloadSet();
}
Wide::Util::optional<ConcreteExpression> Module::AccessMember(ConcreteExpression val, std::string name, Analyzer& a, Lexer::Range where) {
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls.at(name);
        if (auto moddecl = dynamic_cast<const AST::Module*>(decl)) {
            return a.GetWideModule(moddecl, this)->BuildValueConstruction(a, where);
        }
        if (auto usedecl = dynamic_cast<const AST::Using*>(decl)) {
            auto expr = a.AnalyzeExpression(this, usedecl->expr).Resolve(nullptr);
            if (auto conty = dynamic_cast<ConstructorType*>(expr.t->Decay())) {
                return conty->BuildValueConstruction(a, where);
            }
            if (auto fun = dynamic_cast<OverloadSet*>(expr.t->Decay()))
                return expr;
            if (auto temp = dynamic_cast<ClangTemplateClass*>(expr.t->Decay()))
                return expr;
            if (IsLvalueType(expr.t))
                return expr;
            if (auto nam = dynamic_cast<ClangNamespace*>(expr.t->Decay()))
                return expr;
            if (auto mod = dynamic_cast<Module*>(expr.t->Decay()))
                return expr;
            throw std::runtime_error("Attempted to using something that was not a type, template, module, or function");
        }
        if (auto overdecl = dynamic_cast<const AST::FunctionOverloadSet*>(decl))
            return a.GetOverloadSet(overdecl, this)->BuildValueConstruction(a, where); 
        if (auto tydecl = dynamic_cast<const AST::Type*>(decl)) {
            return a.GetConstructorType(a.GetUDT(tydecl, this))->BuildValueConstruction(a, where);
        }
        throw std::runtime_error("Attempted to access a member of a Wide module, but did not recognize it as a using, a type, or a function.");
    }
    if (m->functions.find(name) != m->functions.end())
        return a.GetOverloadSet(m->functions.at(name), this)->BuildValueConstruction(a, where);   
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return SpecialMembers[name];
    return Wide::Util::none;
}