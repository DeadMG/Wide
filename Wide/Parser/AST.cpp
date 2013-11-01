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
                auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>();
                InsertPoint = to->opcondecls[entry.first] = new_set.get();
                owned_overload_sets.insert(std::make_pair(InsertPoint, std::move(new_set)));
            }
            for(auto x : entry.second->functions) {
                auto newfunc = Wide::Memory::MakeUnique<Function>(x->name, x->statements, x->prolog, x->where.front(), x->args, x->initializers);
                inverse[x] = newfunc.get();
                InsertPoint->functions.insert(newfunc.get());
                owned_decl_contexts.insert(std::make_pair(newfunc.get(), std::move(newfunc)));
            }
        }
        for(auto&& entry : from->decls) {
            if (auto nested = dynamic_cast<AST::Module*>(entry.second)) {
                Module* next;
                if (to->functions.find(entry.first) != to->functions.end()) {
                    errors[to].insert(nested);
                    continue;
                }
                if (to->decls.find(entry.first) != to->decls.end())
                    if (auto mod = dynamic_cast<AST::Module*>(to->decls[entry.first]))
                        next = mod;
                    else {
                        errors[to].insert(nested);
                        continue;
                    }
                else {
                    auto new_mod = Wide::Memory::MakeUnique<Module>(entry.first, nested->where.front());
                    new_mod->where = nested->where;
                    to->decls[entry.first] = owned_decl_contexts.insert(std::make_pair(next = new_mod.get(), std::move(new_mod))).first->first;
                }
                adder(next, nested);
                continue;
            }
            if (auto use = dynamic_cast<AST::Using*>(entry.second)) {
                if (to->decls.find(use->name) != to->decls.end()) {
                    errors[to].insert(use);
                    continue;
                }
                if (to->functions.find(use->name) != to->functions.end()) {
                    errors[to].insert(use);
                    continue;
                }
                auto new_using = Wide::Memory::MakeUnique<AST::Using>(use->name, use->expr, use->where.front());
                to->decls[use->name] = owned_decl_contexts.insert(std::make_pair(new_using.get(), std::move(new_using))).first->first;
                continue;
            }
            if (auto ty = dynamic_cast<AST::Type*>(entry.second)) {
                if (to->decls.find(entry.first) != to->decls.end()) {
                    errors[to].insert(ty);
                    continue;
                }
                if (to->functions.find(entry.first) != to->functions.end()) {
                    errors[to].insert(ty);
                    continue;
                }

                auto newty = Wide::Memory::MakeUnique<Type>(entry.first, ty->location);
                newty->variables = ty->variables;
                newty->Functions = ty->Functions;
                newty->opcondecls = ty->opcondecls;
                to->decls[entry.first] = owned_decl_contexts.insert(std::make_pair(newty.get(), std::move(newty))).first->first;
                continue;
            }

            std::vector<Lexer::Range> loc = entry.second->where;
            if (to->decls.find(entry.first) != to->decls.end())
                loc.insert(loc.end(), to->decls.at(entry.first)->where.begin(), to->decls.at(entry.first)->where.end());
        }
        for(auto entry : from->functions) {
            auto overset = entry.second;
            FunctionOverloadSet* InsertPoint = nullptr;
            if (to->decls.find(entry.first) != to->decls.end()) {
                for(auto x : overset->functions)
                    errors[to].insert(x);
                continue;
            }
            if (to->functions.find(entry.first) != to->functions.end()) {
                InsertPoint = to->functions.at(entry.first);
                assert(owned_overload_sets.find(InsertPoint) != owned_overload_sets.end());
            } else {
                auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>();
                InsertPoint = new_set.get();
                to->functions[entry.first] = owned_overload_sets.insert(std::make_pair(InsertPoint, std::move(new_set))).first->first;
            }
            for(auto x : overset->functions) {
                auto newfunc = Wide::Memory::MakeUnique<Function>(x->name, x->statements, x->prolog, x->where.front(), x->args, x->initializers);
                inverse[x] = newfunc.get();
                InsertPoint->functions.insert(newfunc.get());
                owned_decl_contexts.insert(std::make_pair(newfunc.get(), std::move(newfunc)));
            }
        }
    };
    adder(&root, m);    
    for(auto x : errors) {
        std::unordered_map<std::string, std::unordered_set<DeclContext*>> contexts;
        // Sort them by name, and grab the dest.
        for(auto con : x.second) {
            if (x.first->decls.find(con->name) != x.first->decls.end())
                contexts[con->name].insert(x.first->decls.at(con->name));
            if (x.first->functions.find(con->name) != x.first->functions.end())
                for(auto fun : x.first->functions.at(con->name)->functions)
                    contexts[con->name].insert(fun);
            contexts[con->name].insert(con);
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
            for(auto&& fun : overset->functions) {
                assert(inverse.find(fun) != inverse.end());
                to_overset->functions.erase(inverse[fun]);
                inverse.erase(fun);
                owned_decl_contexts.erase(fun);
            }
            if (to_overset->functions.empty()) {
                to->opcondecls.erase(entry.first);
                owned_overload_sets.erase(to_overset);
            }
        }
        for(auto&& entry : from->functions) {
            auto overset = entry.second;
            auto to_overset = to->functions[entry.first];
            assert(to_overset);
            assert(owned_overload_sets.find(to_overset) != owned_overload_sets.end());
            for(auto&& fun : overset->functions) {
                assert(inverse.find(fun) != inverse.end());
                if (errors.find(to) != errors.end())
                    errors.at(to).erase(fun);
                to_overset->functions.erase(inverse[fun]);
                inverse.erase(fun);
                owned_decl_contexts.erase(fun);
            }
            if (to_overset->functions.empty()) {
                to->functions.erase(entry.first);
                owned_overload_sets.erase(to_overset);
            }
            continue;            
        }
        for(auto&& entry : from->decls) {
            if (auto module = dynamic_cast<AST::Module*>(entry.second)) {
                auto to_module = dynamic_cast<AST::Module*>(to->decls[entry.first]);
                assert(to_module);
                assert(owned_decl_contexts.find(to_module) != owned_decl_contexts.end());
                remover(to_module, module);
                if (to_module->decls.empty() && to_module->opcondecls.empty() && to_module->functions.empty()) {
                    to->decls.erase(entry.first);
                    owned_decl_contexts.erase(to_module);
                }
                continue;
            }
            to->decls.erase(entry.first);
            if (errors.find(to) != errors.end())
                errors.at(to).erase(entry.second);

        }
        if (errors.find(to) != errors.end())
            if (errors.at(to).size() == 0)
                errors.erase(to);
    };
    remover(&root, m);
}