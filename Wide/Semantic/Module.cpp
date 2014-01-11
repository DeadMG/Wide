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
    SpecialMembers.insert(std::make_pair(std::move(name), t));
}
OverloadSet* Module::AccessMember(ConcreteExpression val, Wide::Lexer::TokenType ty, Context c) {
    if (m->opcondecls.find(ty) != m->opcondecls.end())
        return c->GetOverloadSet(m->opcondecls.find(ty)->second, this);
    return c->GetOverloadSet();
}
Wide::Util::optional<ConcreteExpression> Module::AccessMember(ConcreteExpression val, std::string name, Context c) {
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls.at(name);
        if (auto moddecl = dynamic_cast<const AST::Module*>(decl)) {
            return c->GetWideModule(moddecl, this)->BuildValueConstruction(c);
        }
        if (auto usedecl = dynamic_cast<const AST::Using*>(decl)) {
            auto expr = c->AnalyzeExpression(this, usedecl->expr, [](ConcreteExpression e) {});
            if (auto conty = dynamic_cast<ConstructorType*>(expr.t->Decay())) {
                return conty->BuildValueConstruction(c);
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
            return c->GetOverloadSet(overdecl, this)->BuildValueConstruction(c); 
        if (auto tydecl = dynamic_cast<const AST::Type*>(decl)) {
            return c->GetConstructorType(c->GetUDT(tydecl, this))->BuildValueConstruction(c);
        }
        throw std::runtime_error("Attempted to access a member of a Wide module, but did not recognize it as a using, a type, or a function.");
    }
    if (m->functions.find(name) != m->functions.end())
        return c->GetOverloadSet(m->functions.at(name), this)->BuildValueConstruction(c);   
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return SpecialMembers.at(name);
    return Wide::Util::none;
}