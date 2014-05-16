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

Module::Module(const AST::Module* p, Module* higher, Analyzer& a)
    : m(p), context(higher), MetaType(a) {}

void Module::AddSpecialMember(std::string name, std::unique_ptr<Expression> t){
    SpecialMembers.insert(std::make_pair(std::move(name), std::move(t)));
}
OverloadSet* Module::CreateOperatorOverloadSet(Type* t, Wide::Lexer::TokenType ty, Lexer::Access access) {
    if (m->opcondecls.find(ty) != m->opcondecls.end()) {
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto func : m->opcondecls.at(ty)->functions) {
            if (func->access > access)
                continue;
            resolvable.insert(analyzer.GetCallableForFunction(func, this, GetNameForOperator(ty)));
        }
        if (resolvable.empty()) return PrimitiveType::CreateOperatorOverloadSet(t, ty, access);
        return analyzer.GetOverloadSet(resolvable);
    }
    return PrimitiveType::CreateOperatorOverloadSet(t, ty, access);
}
std::unique_ptr<Expression> Module::AccessMember(std::unique_ptr<Expression> val, std::string name, Context c)  {
    auto access = GetAccessSpecifier(c.from, this);
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls.at(name);
        if (decl->access > access)
            return nullptr;

        if (auto moddecl = dynamic_cast<const AST::Module*>(decl)) {
            return analyzer.GetWideModule(moddecl, this)->BuildValueConstruction({}, c);
        }
        if (auto usedecl = dynamic_cast<const AST::Using*>(decl)) {
            auto expr = AnalyzeExpression(this, usedecl->expr, analyzer);
            if (auto constant = expr->GetType()->Decay()->GetConstantContext())
                return constant->BuildValueConstruction({}, c);
            throw BadUsingTarget(expr->GetType()->Decay(), c.where);
        }
        if (auto overdecl = dynamic_cast<const AST::FunctionOverloadSet*>(decl)) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto func : overdecl->functions) {
                if (func->access > access)
                    continue;
                resolvable.insert(analyzer.GetCallableForFunction(func, this, name));
            }
            if (resolvable.empty()) return nullptr;
            return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, c);
        }
        if (auto temptydecl = dynamic_cast<const AST::TemplateTypeOverloadSet*>(decl)) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto func : temptydecl->templatetypes) {
                if (func->t->access > access)
                    continue;
                resolvable.insert(analyzer.GetCallableForTemplateType(func, this));
            }
            if (resolvable.empty()) return nullptr;
            return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, c);
        }
        if (auto tydecl = dynamic_cast<const AST::Type*>(decl)) {
            return analyzer.GetConstructorType(analyzer.GetUDT(tydecl, this, name))->BuildValueConstruction({}, c);
        }
        assert(false && "Looked up a member of a module but did not recognize the AST node used.");
    }
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return Wide::Memory::MakeUnique<ExpressionReference>(SpecialMembers.at(name).get());
    return nullptr;
}
std::string Module::explain() {
    if (!context) return "";
    std::string name;
    for (auto decl : context->GetASTModule()->decls) {
        if (decl.second == m)
            name = decl.first;
    }
    if (context == analyzer.GetGlobalModule())
        return name;
    return context->explain() + "." + name;
}