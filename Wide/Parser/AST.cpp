#include <Wide/Parser/AST.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <functional>
#include <cassert>

using namespace Wide;
using namespace AST;

void Combiner::Add(Module* m) {
    assert(modules.find(m) == modules.end());
    modules.insert(m);
    auto adder = std::function<void(Module*, Module*)>();
    adder = [&, this](Module* to, Module* from) {
        assert(owned_decl_contexts.find(from) == owned_decl_contexts.end());
        assert(owned_decl_contexts.find(to) != owned_decl_contexts.end() || to == &root);
        for(auto&& entry : from->opcondecls) {
            FunctionOverloadSet* InsertPoint = nullptr;
            if (to->opcondecls.find(entry.first) != to->opcondecls.end()) {
                InsertPoint = to->opcondecls.at(entry.first);
                assert(owned_overload_sets.find(InsertPoint) != owned_overload_sets.end());
            } else {
                auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>(entry.second->where.front());
                InsertPoint = to->opcondecls[entry.first] = new_set.get();
                owned_overload_sets.insert(std::make_pair(InsertPoint, std::move(new_set)));
            }
            for(auto x : entry.second->functions)
                InsertPoint->functions.insert(x);
        }
        for(auto&& entry : from->decls) {
            if (auto nested = dynamic_cast<AST::Module*>(entry.second)) {
                Module* next;
                if (to->decls.find(entry.first) != to->decls.end())
                    if (auto mod = dynamic_cast<AST::Module*>(to->decls[entry.first]))
                        next = mod;
                    else {
                        errors[to][entry.first].insert(nested);
                        continue;
                    }
                else {
                    auto new_mod = Wide::Memory::MakeUnique<Module>(nested->where.front(), nested->access);
                    new_mod->where = nested->where;
                    to->decls[entry.first] = owned_decl_contexts.insert(std::make_pair(next = new_mod.get(), std::move(new_mod))).first->first;
                }
                adder(next, nested);
                continue;
            }
            if (auto use = dynamic_cast<AST::Using*>(entry.second)) {
                if (to->decls.find(entry.first) != to->decls.end()) {
                    errors[to][entry.first].insert(use);
                    continue;
                }
                to->decls[entry.first] = use;
                continue;
            }
            if (auto ty = dynamic_cast<AST::Type*>(entry.second)) {
                if (to->decls.find(entry.first) != to->decls.end()) {
                    errors[to][entry.first].insert(ty);
                    continue;
                }
                to->decls[entry.first] = ty;
                continue;
            }
            if (auto overset = dynamic_cast<AST::FunctionOverloadSet*>(entry.second)) {
                if (to->decls.find(entry.first) != to->decls.end()) {
                    if (auto toset = dynamic_cast<AST::FunctionOverloadSet*>(to->decls.at(entry.first))) {
                        toset->functions.insert(overset->functions.begin(), overset->functions.end());
                    }
                }
                else {
                    auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>(entry.second->where.front());
                    new_set->functions.insert(overset->functions.begin(), overset->functions.end());
                    to->decls[entry.first] = new_set.get();
                    owned_overload_sets.insert(std::make_pair(new_set.get(), std::move(new_set)));
                }
            }

            std::vector<Lexer::Range> loc = entry.second->where;
            if (to->decls.find(entry.first) != to->decls.end())
                loc.insert(loc.end(), to->decls.at(entry.first)->where.begin(), to->decls.at(entry.first)->where.end());
        }
    };
    adder(&root, m);    
    for(auto x : errors) {
        std::unordered_map<std::string, std::unordered_set<DeclContext*>> contexts;
        // Sort them by name, and grab the dest.
        for(auto con : x.second) {
            if (x.first->decls.find(con.first) != x.first->decls.end())
                contexts[con.first].insert(x.first->decls.at(con.first));
            contexts[con.first].insert(con.second.begin(), con.second.end());
        }
        
        // Issue each name as a separate list of errors.
        for(auto list : contexts) {
            auto vec = std::vector<std::pair<Lexer::Range, DeclContext*>>();
            for(auto con : list.second)
                for(auto loc : con->where)
                    vec.push_back(std::make_pair(loc, con));
            error(std::move(vec));
        }
    }
}

void Combiner::Remove(Module* m) {
    assert(modules.find(m) != modules.end());
    modules.erase(m);
    auto remover = std::function<void(Module*, Module*)>();
    remover = [&, this](Module* to, Module* from) {
        assert(owned_decl_contexts.find(from) == owned_decl_contexts.end());
        assert(owned_decl_contexts.find(to) != owned_decl_contexts.end() || to == &root);
        for(auto&& entry : from->opcondecls) {
            auto overset = entry.second;
            auto to_overset = to->opcondecls.at(entry.first);
            assert(to_overset);
            assert(owned_overload_sets.find(to_overset) != owned_overload_sets.end());
            for(auto&& fun : overset->functions)
                to_overset->functions.erase(fun);
            if (to_overset->functions.empty()) {
                to->opcondecls.erase(entry.first);
                owned_overload_sets.erase(to_overset);
            }
        }
        for(auto&& entry : from->decls) {
            if (auto module = dynamic_cast<AST::Module*>(entry.second)) {
                auto to_module = dynamic_cast<AST::Module*>(to->decls[entry.first]);
                assert(to_module);
                assert(owned_decl_contexts.find(to_module) != owned_decl_contexts.end());
                remover(to_module, module);
                if (to_module->decls.empty() && to_module->opcondecls.empty()) {
                    to->decls.erase(entry.first);
                    owned_decl_contexts.erase(to_module);
                }
                continue;
            }
            to->decls.erase(entry.first);
            if (errors.find(to) != errors.end())
                errors.at(to).at(entry.first).erase(entry.second);
        }
        if (errors.find(to) != errors.end())
            if (errors.at(to).size() == 0)
                errors.erase(to);
    };
    remover(&root, m);
}