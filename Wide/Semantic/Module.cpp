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
                    resolvable.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, nullptr, [](Wide::Parse::Name, Wide::Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>) { return nullptr; })));
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
        if (auto shared = boost::get<std::pair<Parse::Access, std::shared_ptr<Parse::SharedObject>>>(&m->named_decls.at(name))) {
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
        if (!expr->IsConstant(Expression::NoInstance()))
            return CreateErrorExpression(Memory::MakeUnique<SpecificError<UsingTargetNotConstant>>(analyzer, usedecl->expr->location, "Using target not a constant expression."));
        return expr;
    });

    AddHandler<const Parse::Type>(a.SharedObjectHandlers, [](const Parse::Type* type, Analyzer& analyzer, Module* lookup, std::string name) {
        return analyzer.GetConstructorType(analyzer.GetUDT(type, lookup, name))->BuildValueConstruction(Expression::NoInstance(), {}, { lookup, type->location });
    });

    AddHandler<const Parse::Module>(a.UniqueObjectHandlers, [](const Parse::Module* mod, Analyzer& analyzer, Module* lookup, std::string name) {
        return analyzer.GetWideModule(mod, lookup, name)->BuildValueConstruction(Expression::NoInstance(), {}, { lookup, mod->locations.begin()->GetIdentifier() });
    });

    AddHandler<const Parse::ModuleOverloadSet<Parse::Function>>(a.MultiObjectHandlers, [](const Parse::ModuleOverloadSet<Parse::Function>* overset, Analyzer& analyzer, Module* lookup, Parse::Access access, std::string name, Lexer::Range where) -> std::shared_ptr<Expression> {
        std::unordered_set<OverloadResolvable*> resolvable;
        for (auto&& map : overset->funcs) {
            if (map.first > access)
                continue;
            for (auto&& func : map.second)
                resolvable.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), lookup, nullptr, [](Wide::Parse::Name, Wide::Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>) { return nullptr; })));
        }
        if (resolvable.empty()) return nullptr;
        return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction(Expression::NoInstance(), {}, { lookup, where });
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
        return analyzer.GetOverloadSet(resolvable)->BuildValueConstruction(Expression::NoInstance(), {}, { lookup, where });
    });
}