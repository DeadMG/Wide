#include <Wide/Semantic/Module.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/Expression.h>

using namespace Wide;
using namespace Semantic;

Module::Module(const Parse::Module* p, Module* higher, Analyzer& a)
    : m(p), context(higher), MetaType(a) {}

void Module::AddSpecialMember(std::string name, std::shared_ptr<Expression> t){
    SpecialMembers.insert(std::make_pair(std::move(name), std::move(t)));
}
OverloadSet* Module::CreateOperatorOverloadSet(Type* t, Wide::Lexer::TokenType ty, Lexer::Access access) {
    auto name = "operator" + *ty;
    if (m->named_decls.find(name) != m->named_decls.end()) {
        auto overset = boost::get<std::unordered_map<Lexer::Access, std::unordered_set<Parse::Function*>>>(m->named_decls.at(name));
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto set : overset) {
            for (auto func : set.second) {
                if (set.first > access)
                    continue;
                resolvable.insert(analyzer.GetCallableForFunction(func, this, name));
            }
        }
        if (resolvable.empty()) return PrimitiveType::CreateOperatorOverloadSet(t, ty, access);
        return analyzer.GetOverloadSet(resolvable);
    }
    return PrimitiveType::CreateOperatorOverloadSet(t, ty, access);
}
std::shared_ptr<Expression> Module::AccessMember(std::shared_ptr<Expression> val, std::string name, Context c)  {
    auto access = GetAccessSpecifier(c.from, this);
    if (m->named_decls.find(name) != m->named_decls.end()) {
        if (auto mod = boost::get<std::pair<Lexer::Access, Parse::Module*>>(&m->named_decls.at(name))) {
            if (mod->first > access)
                return nullptr;
            return BuildChain(std::move(val), analyzer.GetWideModule(mod->second, this)->BuildValueConstruction({}, c));
        }
        if (auto usedecl = boost::get<std::pair<Lexer::Access, Parse::Using*>>(&m->named_decls.at(name))) {
            if (usedecl->first > access)
                return nullptr;
            auto expr = analyzer.AnalyzeCachedExpression(this, usedecl->second->expr);
            if (auto constant = expr->GetType()->Decay()->GetConstantContext())
                return BuildChain(std::move(val), constant->BuildValueConstruction({}, c));
            throw BadUsingTarget(expr->GetType()->Decay(), usedecl->second->expr->location);
        }
        if (auto tydecl = boost::get<std::pair<Lexer::Access, Parse::Type*>>(&m->named_decls.at(name))) {
            if (tydecl->first > access)
                return nullptr;
            return BuildChain(std::move(val), analyzer.GetConstructorType(analyzer.GetUDT(tydecl->second, this, name))->BuildValueConstruction({}, c));
        }
        if (auto overdecl = boost::get<std::unordered_map<Lexer::Access, std::unordered_set<Parse::Function*>>>(&m->named_decls.at(name))) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto map : *overdecl) {
                if (map.first > access)
                    continue;
                for (auto func : map.second)
                    resolvable.insert(analyzer.GetCallableForFunction(func, this, name));
            }
            if (resolvable.empty()) return nullptr;
            return BuildChain(std::move(val), analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, c));
        }
        if (auto overdecl = boost::get<std::unordered_map<Lexer::Access, std::unordered_set<Parse::TemplateType*>>>(&m->named_decls.at(name))) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto map : *overdecl) {
                if (map.first > access)
                    continue;
                for (auto func : map.second)
                    resolvable.insert(analyzer.GetCallableForTemplateType(func, this));
            }
            if (resolvable.empty()) return nullptr;
            return BuildChain(std::move(val), analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, c));
        }
    }
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return BuildChain(std::move(val), SpecialMembers[name]);
    return nullptr;
}
std::string Module::explain() {
    if (!context) return "";
    std::string name;
    for (auto decl : context->GetASTModule()->named_decls) {
        if (auto mod = boost::get<std::pair<Lexer::Access, Parse::Module*>>(&decl.second))
            name = decl.first;
    }
    if (context == analyzer.GetGlobalModule())
        return name;
    return context->explain() + "." + name;
}