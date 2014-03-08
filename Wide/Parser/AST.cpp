#include <Wide/Parser/AST.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <functional>
#include <cassert>

using namespace Wide;
using namespace AST;

void Combiner::CombinedModule::AddModuleToSelf(Module* mod) {
    for (auto decl : mod->decls)
        Add(decl.second, decl.first);

    for (auto&& entry : mod->opcondecls) {
        FunctionOverloadSet* InsertPoint = nullptr;
        if (opcondecls.find(entry.first) != opcondecls.end()) {
            InsertPoint = opcondecls.at(entry.first);
        } else {
            operator_overload_sets[entry.first] = Wide::Memory::MakeUnique<FunctionOverloadSet>(entry.second->where.front());
            InsertPoint = operator_overload_sets[entry.first].get();
            opcondecls[entry.first] = operator_overload_sets[entry.first].get();
        }
        for (auto x : entry.second->functions)
            InsertPoint->functions.insert(x);
    }
}

void Combiner::CombinedModule::Add(DeclContext* d, std::string name) {
    if (auto mod = dynamic_cast<Module*>(d)) {
        CombinedModule* combmod = nullptr;
        if (combined_modules.find(name) != combined_modules.end()) {
            combmod = combined_modules[name].get();
            combmod->where.insert(combmod->where.end(), d->where.begin(), d->where.end());
        } else {
            combmod = (combined_modules[name] = Wide::Memory::MakeUnique<CombinedModule>(mod->where.front(), mod->access)).get();
            combmod->where = mod->where;
            if (decls.find(name) != decls.end())
                errors[name].insert(combmod);
            else
                decls[name] = combmod;
        }
        combmod->AddModuleToSelf(mod);
        // Don't care whether it's an error or not just add the stuff
        return;
    }

    if (auto overset = dynamic_cast<AST::FunctionOverloadSet*>(d)) {
        FunctionOverloadSet* InsertPoint = nullptr;
        if (combined_overload_sets.find(name) != combined_overload_sets.end()) {
            InsertPoint = combined_overload_sets[name].get();
        } else {
            InsertPoint = (combined_overload_sets[name] = Wide::Memory::MakeUnique<FunctionOverloadSet>(d->where.front())).get();
            if (decls.find(name) != decls.end())
                errors[name].insert(InsertPoint);
            else
                decls[name] = InsertPoint;
        }
        InsertPoint->functions.insert(overset->functions.begin(), overset->functions.end());
        return;
    }

    if (decls.find(name) == decls.end())
        decls[name] = d;
    else
        errors[name].insert(d);
}
void Combiner::CombinedModule::Remove(DeclContext* d, std::string name) {
    auto PopDecl = [this](DeclContext* con, std::string name) {
        if (decls.at(name) == con) {
            decls.erase(name);
            if (errors.find(name) != errors.end()) {
                decls[name] = *errors[name].begin();
                errors[name].erase(errors[name].begin());
                if (errors[name].empty())
                    errors.erase(name);
            }
            return;
        }
        if (errors.find(name) != errors.end())
            errors[name].erase(con);
        if (errors[name].empty())
            errors.erase(name);
        return;
    };
    if (auto mod = dynamic_cast<Module*>(d)) {
        assert(combined_modules.find(name) != combined_modules.end());
        auto combmod = combined_modules[name].get();
        combmod->RemoveModuleFromSelf(mod);
        if (combmod->decls.empty() && combmod->opcondecls.empty()) {
            PopDecl(combmod, name);
            combined_modules.erase(name);
        }
        return;
    }

    if (auto overset = dynamic_cast<FunctionOverloadSet*>(d)) {
        assert(combined_overload_sets.find(name) != combined_overload_sets.end());
        for (auto f : overset->functions)
            combined_overload_sets[name]->functions.erase(f);
        if (combined_overload_sets[name]->functions.empty()) {
            PopDecl(combined_overload_sets[name].get(), name);
            combined_overload_sets.erase(name);
        }
        return;
    }

    PopDecl(d, name);
}
void Combiner::CombinedModule::RemoveModuleFromSelf(Module* mod) {
    for (auto decl : mod->decls)
        Remove(decl.second, decl.first);

    for (auto&& entry : mod->opcondecls) {
        assert(opcondecls.find(entry.first) != opcondecls.end());
        assert(operator_overload_sets.find(entry.first) != operator_overload_sets.end());
        FunctionOverloadSet* InsertPoint = operator_overload_sets[entry.first].get();
        for (auto x : entry.second->functions) {
            assert(InsertPoint->functions.count(x));
            InsertPoint->functions.erase(x);
        }
        if (InsertPoint->functions.empty()) {
            operator_overload_sets.erase(entry.first);
            opcondecls.erase(entry.first);
        }
    }
}
void Combiner::CombinedModule::ReportErrors(std::function<void(std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>&)> error, 
    std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>& scratch) {
    for (auto x : errors) {
        // Issue each name as a separate list of errors.
        for (auto&& list : errors) {
            for (auto con : list.second)
                for (auto&& loc : con->where)
                    scratch.push_back(std::make_pair(&loc, con));
            error(scratch);
            scratch.clear();
        }
    }
    // Recursively report our submodules- even the error ones.
    for (auto&& x : combined_modules)
        x.second->ReportErrors(error, scratch);
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
void Combiner::ReportErrors(std::function<void(std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>&)> error) {
    std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>> scratch;
    scratch.reserve(10);
    root.ReportErrors(error, scratch);
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