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

Module::Module(const Parse::Module* p, Module* higher, std::string name, Analyzer& a)
    : m(p), context(higher), name(name), MetaType(a) {}

void Module::AddSpecialMember(std::string name, std::shared_ptr<Expression> t){
    SpecialMembers.insert(std::make_pair(std::move(name), std::move(t)));
}
OverloadSet* Module::CreateOperatorOverloadSet(Parse::OperatorName ty, Parse::Access access, OperatorAccess kind) {
    if (kind == OperatorAccess::Explicit) {
        if (m->OperatorOverloads.find(ty) != m->OperatorOverloads.end()) {
            auto overset = m->OperatorOverloads.at(ty);
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto set : overset) {
                for (auto func : set.second) {
                    if (set.first > access)
                        continue;
                    resolvable.insert(analyzer.GetCallableForFunction(func.get(), this, GetOperatorName(ty), [](Wide::Parse::Name, Wide::Lexer::Range) { return nullptr; }));
                }
            }
            return analyzer.GetOverloadSet(resolvable);
        }
    }
    return PrimitiveType::CreateOperatorOverloadSet(ty, access, kind);
}
std::shared_ptr<Expression> Module::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::string name, Context c)  {
    auto access = GetAccessSpecifier(c.from, this);
    if (m->named_decls.find(name) != m->named_decls.end()) {
        if (auto shared = boost::get<std::pair<Parse::Access, std::shared_ptr<Parse::SharedObjectTag>>>(&m->named_decls.at(name))) {
            if (shared->first > access) return nullptr;
            return BuildChain(std::move(val), analyzer.SharedObjectHandlers.at(typeid(*shared->second))(shared->second.get(), analyzer, this, name));
        }
        if (auto unique = boost::get<std::pair<Parse::Access, std::unique_ptr<Parse::UniqueAccessContainer>>>(&m->named_decls.at(name))) {
            if (unique->first > access) return nullptr;
            return BuildChain(std::move(val), analyzer.UniqueObjectHandlers.at(typeid(*unique->second))(unique->second.get(), analyzer, this, name));
        }
        auto&& multi = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(m->named_decls.at(name));
        return BuildChain(std::move(val), analyzer.MultiObjectHandlers.at(typeid(*multi))(multi.get(), analyzer, this, access, name, c.where));
    }    
    if (SpecialMembers.find(name) != SpecialMembers.end())
        return BuildChain(std::move(val), SpecialMembers[name]);
    return nullptr;
}
std::string Module::explain() {
    if (!context) return ".";
    if (context == analyzer.GetGlobalModule())
        return "." + name;
    return context->explain() + "." + name;
}
void Module::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::Using>(a.SharedObjectHandlers, [](const Parse::Using* usedecl, Analyzer& analyzer, Module* lookup, std::string name) {
        auto expr = analyzer.AnalyzeExpression(lookup, usedecl->expr.get(), [](Wide::Parse::Name, Wide::Lexer::Range) { return nullptr; });
        if (auto constant = expr->GetType(nullptr)->Decay()->GetConstantContext())
            return constant->BuildValueConstruction({}, { lookup, usedecl->location });
        throw BadUsingTarget(expr->GetType(nullptr)->Decay(), usedecl->expr->location);
    });

    AddHandler<const Parse::Type>(a.SharedObjectHandlers, [](const Parse::Type* type, Analyzer& analyzer, Module* lookup, std::string name) {
        return analyzer.GetConstructorType(analyzer.GetUDT(type, lookup, name))->BuildValueConstruction({}, { lookup, type->location });
    });

    AddHandler<const Parse::Module>(a.UniqueObjectHandlers, [](const Parse::Module* mod, Analyzer& analyzer, Module* lookup, std::string name) {
        return analyzer.GetWideModule(mod, lookup, name)->BuildValueConstruction({}, { lookup, *mod->locations.begin() });
    });

    AddHandler<const Parse::ModuleOverloadSet<Parse::Function>>(a.MultiObjectHandlers, [](const Parse::ModuleOverloadSet<Parse::Function>* overset, Analyzer& analyzer, Module* lookup, Parse::Access access, std::string name, Lexer::Range where) -> std::shared_ptr<Expression> {
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto&& map : overset->funcs) {
            if (map.first > access)
                continue;
            for (auto&& func : map.second)
                resolvable.insert(analyzer.GetCallableForFunction(func.get(), lookup, name, [](Wide::Parse::Name, Wide::Lexer::Range) { return nullptr; }));
        }
        if (resolvable.empty()) return nullptr;
        return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, { lookup, where });
    });

    AddHandler<const Parse::ModuleOverloadSet<Parse::TemplateType>>(a.MultiObjectHandlers, [](const Parse::ModuleOverloadSet<Parse::TemplateType>* overset, Analyzer& analyzer, Module* lookup, Parse::Access access, std::string name, Lexer::Range where) -> std::shared_ptr<Expression> {
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto&& map : overset->funcs) {
            if (map.first > access)
                continue;
            for (auto&& func : map.second)
                resolvable.insert(analyzer.GetCallableForTemplateType(func.get(), lookup));
        }
        if (resolvable.empty()) return nullptr;
        return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction({}, { lookup, where });
    });
}