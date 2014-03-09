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
#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

Module::Module(const AST::Module* p, Module* higher) 
    : m(p), context(higher) {}

void Module::AddSpecialMember(std::string name, ConcreteExpression t){
    SpecialMembers.insert(std::make_pair(std::move(name), t));
}
OverloadSet* Module::CreateOperatorOverloadSet(Type* t, Wide::Lexer::TokenType ty, Lexer::Access access, Analyzer& a) {
    if (m->opcondecls.find(ty) != m->opcondecls.end()) {
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto func : m->opcondecls.at(ty)->functions) {
            if (func->access > access)
                continue;
            resolvable.insert(a.GetCallableForFunction(func, this, GetNameForOperator(ty)));
        }
        if (resolvable.empty()) return PrimitiveType::CreateOperatorOverloadSet(t, ty, access, a);
        return a.GetOverloadSet(resolvable);
    }
    return PrimitiveType::CreateOperatorOverloadSet(t, ty, access, a);
}
Wide::Util::optional<ConcreteExpression> Module::AccessMember(ConcreteExpression val, std::string name, Context c) {
    auto access = GetAccessSpecifier(c, this);
    if (CachedLookups.find(name) != CachedLookups.end())
        return CachedLookups.at(name);
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls.at(name);
        if (decl->access > access)
            return Wide::Util::none;

        if (auto moddecl = dynamic_cast<const AST::Module*>(decl)) {
            return c->GetWideModule(moddecl, this)->BuildValueConstruction({}, c);
        }
        if (auto usedecl = dynamic_cast<const AST::Using*>(decl)) {
            auto expr = c->AnalyzeExpression(this, usedecl->expr, [](ConcreteExpression e) {});
            if (auto conty = dynamic_cast<ConstructorType*>(expr.t->Decay())) {
                return conty->BuildValueConstruction({}, c);
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
        if (auto overdecl = dynamic_cast<const AST::FunctionOverloadSet*>(decl)) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto func : overdecl->functions) {
                if (func->access > access)
                    continue;
                resolvable.insert(c->GetCallableForFunction(func, this, name));
            }
            if (resolvable.empty()) return Wide::Util::none;
            return c->GetOverloadSet(resolvable)->BuildValueConstruction({}, c);
        }
        if (auto temptydecl = dynamic_cast<const AST::TemplateTypeOverloadSet*>(decl)) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto func : temptydecl->templatetypes) {
                if (func->t->access > access)
                    continue;
                resolvable.insert(c->GetCallableForTemplateType(func, this));
            }
            if (resolvable.empty()) return Wide::Util::none;
            return c->GetOverloadSet(resolvable)->BuildValueConstruction({}, c);
        }
        if (auto tydecl = dynamic_cast<const AST::Type*>(decl)) {
            return c->GetConstructorType(c->GetUDT(tydecl, this, name))->BuildValueConstruction({}, c);
        }
        throw std::runtime_error("Attempted to access a member of a Wide module, but did not recognize it as a using, a type, or a function.");
    }
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return SpecialMembers.at(name);
    return Wide::Util::none;
}
std::string Module::explain(Analyzer& a) {
    if (!context) return "";
    std::string name;
    for (auto decl : context->GetASTModule()->decls) {
        if (decl.second == m)
            name = decl.first;
    }
    return context->explain(a) + "." + name;
}