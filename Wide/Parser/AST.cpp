#include <Wide/Parser/AST.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <functional>
#include <cassert>

using namespace Wide;
using namespace Parse;

void Combiner::CombinedModule::AddModuleToSelf(Module* mod) {
    constructor_decls.insert(mod->constructor_decls.begin(), mod->constructor_decls.end());
    destructor_decls.insert(mod->destructor_decls.begin(), mod->destructor_decls.end());
    for (auto decl : mod->named_decls) {
        // If we have a module, then union the respective combined module and that module.
        if (auto mod = boost::get<std::pair<Parse::Access, Module*>>(&decl.second)) {
            if (combined_modules.find(decl.first) == combined_modules.end()) {
                combined_modules[decl.first] = Wide::Memory::MakeUnique<CombinedModule>();
                if (named_decls.find(decl.first) == named_decls.end())
                    named_decls[decl.first] = std::make_pair(mod->first, combined_modules[decl.first].get());
            }
            combined_modules[decl.first]->AddModuleToSelf(mod->second);
        }  else {
            // If we have nothing, then the union of nothing and a thing is that thing.
            if (named_decls.find(decl.first) == named_decls.end()) {
                named_decls[decl.first] = decl.second;
                continue;
            }

            // Union function and template overload sets.
            if (auto theiroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Function*>>>(&decl.second)) {
                if (auto ouroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Function*>>>(&named_decls[decl.first])) {
                    for (auto&& access : *theiroverset)
                        (*ouroverset)[access.first].insert(access.second.begin(), access.second.end()); 
                }
            }
            if (auto theiroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>>(&decl.second)) {
                if (auto ouroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>>(&named_decls[decl.first])) {
                    for (auto&& access : *theiroverset)
                        (*ouroverset)[access.first].insert(access.second.begin(), access.second.end());
                }
            }
        }
    }
    for (auto name : mod->OperatorOverloads) {
        for (auto access : name.second) {
            for (auto op : access.second) {
                OperatorOverloads[name.first][access.first].insert(op);
            }
        }
    }
}

void Combiner::CombinedModule::RemoveModuleFromSelf(Module* mod) {
    for (auto&& decl : mod->constructor_decls)
        constructor_decls.erase(decl);
    for (auto&& decl : mod->destructor_decls)
        destructor_decls.erase(decl);

    for (auto decl : mod->named_decls) {
        if (auto othermod = boost::get<std::pair<Parse::Access, Module*>>(&decl.second)) {
            auto&& combmod = combined_modules[decl.first].get();
            combmod->RemoveModuleFromSelf(othermod->second);
            if (combmod->constructor_decls.empty() && combmod->destructor_decls.empty() && combmod->named_decls.empty()) {
                // It's empty- time to kill it.
                if (boost::get<std::pair<Parse::Access, Module*>>(&decl.second))
                    named_decls.erase(decl.first);
                combined_modules.erase(decl.first);
            }
        }

        if (auto theiroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Function*>>>(&decl.second)) {
            if (auto ouroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Function*>>>(&named_decls[decl.first])) {
                for (auto&& access : *theiroverset) {
                    for (auto&& func : access.second) {
                        (*ouroverset)[access.first].erase(func);
                    }
                    if (access.second.empty())
                        ouroverset->erase(access.first);
                }
                if (ouroverset->empty())
                    named_decls.erase(decl.first);
            }
        }
        if (auto theiroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>>(&decl.second)) {
            if (auto ouroverset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>>(&named_decls[decl.first])) {
                for (auto&& access : *theiroverset) {
                    for (auto&& func : access.second) {
                        (*ouroverset)[access.first].erase(func);
                    }
                    if (access.second.empty())
                        ouroverset->erase(access.first);
                }
                if (ouroverset->empty())
                    named_decls.erase(decl.first);
            }
        }

        if (auto theirusing = boost::get<std::pair<Parse::Access, Using*>>(&decl.second))
            if (auto ourusing = boost::get<std::pair<Parse::Access, Using*>>(&named_decls[decl.first]))
                named_decls.erase(decl.first);

        if (auto theirtype = boost::get<std::pair<Parse::Access, Type*>>(&decl.second))
            if (auto ourtype = boost::get<std::pair<Parse::Access, Type*>>(&named_decls[decl.first]))
                named_decls.erase(decl.first);
    }
    for (auto name : mod->OperatorOverloads) {
        for (auto access : name.second) {
            for (auto op : access.second) {
                OperatorOverloads[name.first][access.first].erase(op);
                if (OperatorOverloads[name.first][access.first].empty())
                    OperatorOverloads[name.first].erase(access.first);
                if (OperatorOverloads[name.first].empty())
                    OperatorOverloads.erase(name.first);
            }
        }
    }
}
void Combiner::Add(Module* mod) {
    assert(modules.find(mod) == modules.end());
    modules.insert(mod);
    root.AddModuleToSelf(mod);
}
void Combiner::Remove(Module* mod) {
    assert(modules.find(mod) != modules.end());
    modules.erase(mod);
    root.RemoveModuleFromSelf(mod);
}
void Combiner::SetModules(std::unordered_set<Module*> mods) {
    std::vector<Module*> removals;
    for (auto mod : modules) {
        if (mods.find(mod) == mods.end())
            removals.push_back(mod);
    }
    for (auto mod : removals)
        Remove(mod);
    for (auto mod : mods) {
        if (modules.find(mod) == modules.end())
            Add(mod);
    }
}