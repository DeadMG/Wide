#include <Wide/Parser/AST.h>
#include <Wide/Util/MakeUnique.h>
#include <functional>
#include <cassert>

using namespace Wide;
using namespace AST;

void Combiner::Add(Module* m) {
    auto adder = std::function<void(Module*, Module*)>();
    adder = [&, this](Module* to, Module* from) {
        assert(owned_decl_contexts.find(from) == owned_decl_contexts.end());
        assert(owned_decl_contexts.find(to) != owned_decl_contexts.end() || to == &root);
        for(auto&& entry : from->opcondecls) {
            FunctionOverloadSet* InsertPoint;
            if (to->opcondecls.find(entry.first) != to->opcondecls.end()) {
                InsertPoint = to->opcondecls.at(entry.first);
            } else {
                auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>("operator", to);
                owned_decl_contexts.insert(std::make_pair(InsertPoint = to->opcondecls[entry.first] = new_set.get(), std::move(new_set))).first->first;
            }
            InsertPoint->functions.insert(entry.second->functions.begin(), entry.second->functions.end());
        }
        for(auto&& entry : from->decls) {
            if (auto nested = dynamic_cast<AST::Module*>(entry.second)) {
                Module* next;
                if (to->decls.find(nested->name) != to->decls.end())
                    if (auto mod = dynamic_cast<AST::Module*>(to->decls[nested->name]))
                        next = mod;
                    else
                        throw std::runtime_error("Attempted to add a module to another module, but there was already an object of that name that was not a module.");
                else {
                    auto new_mod = Wide::Memory::MakeUnique<Module>(nested->name, to);
                    to->decls[nested->name] = owned_decl_contexts.insert(std::make_pair(next = new_mod.get(), std::move(new_mod))).first->first;
                }
                adder(next, nested);
                continue;
            }
            if (auto use = dynamic_cast<AST::Using*>(entry.second)) {
                if (to->decls.find(use->name) != to->decls.end())
                    throw std::runtime_error("Attempted to insert a using into a module, but there was already another declaration there.");
                auto new_using = Wide::Memory::MakeUnique<AST::Using>(use->name, use->expr, to);
                to->decls[use->name] = owned_decl_contexts.insert(std::make_pair(new_using.get(), std::move(new_using))).first->first;
                continue;
            }
            if (auto overset = dynamic_cast<AST::FunctionOverloadSet*>(entry.second)) {
                FunctionOverloadSet* InsertPoint;
                if (to->decls.find(overset->name) != to->decls.end())
                    if (auto existing = dynamic_cast<AST::FunctionOverloadSet*>(to->decls[overset->name]))
                        InsertPoint = existing;
                    else
                        throw std::runtime_error("Attempted to add a function overload set to a module, but there was already another object there that was not an overload set.");
                else {
                    auto new_set = Wide::Memory::MakeUnique<FunctionOverloadSet>(overset->name, to);
                    to->decls[overset->name] = owned_decl_contexts.insert(std::make_pair(InsertPoint = new_set.get(), std::move(new_set))).first->first;
                }
                for(auto x : overset->functions) {
                    auto newfunc = Wide::Memory::MakeUnique<Function>(x->name, x->statements, x->prolog, x->location, x->args, InsertPoint, x->initializers);
                    InsertPoint->functions.insert(newfunc.get());
                    owned_decl_contexts.insert(std::make_pair(newfunc.get(), std::move(newfunc)));
                }
                continue;
            }
            if (auto ty = dynamic_cast<AST::Type*>(entry.second)) {
                auto newty = Wide::Memory::MakeUnique<Type>(to, entry.first, ty->location);
                newty->variables = ty->variables;
                newty->Functions = ty->Functions;
                newty->opcondecls = ty->opcondecls;
                to->decls[entry.first] = owned_decl_contexts.insert(std::make_pair(newty.get(), std::move(newty))).first->first;
                continue;
            }

            if (to->decls.find(entry.first) != to->decls.end())
                throw std::runtime_error("Attempted to add an object to a module, but that module already had an object in that slot.");
            to->decls[entry.first] = entry.second;
        }
    };
    adder(&root, m);
}

void Combiner::Remove(Module* m) {
    auto remover = std::function<void(Module*, Module*)>();
    remover = [&, this](Module* to, Module* from) {
        assert(owned_decl_contexts.find(from) == owned_decl_contexts.end());
        assert(owned_decl_contexts.find(to) != owned_decl_contexts.end() || to == &root);
        for(auto&& entry : from->opcondecls) {
            auto overset = entry.second;
            auto to_overset = dynamic_cast<AST::FunctionOverloadSet*>(to->opcondecls[entry.first]);
            assert(to_overset);
            assert(owned_decl_contexts.find(to_overset) != owned_decl_contexts.end());
            for(auto&& fun : overset->functions)
                to_overset->functions.erase(fun);
            if (to_overset->functions.empty()) {
                to->opcondecls.erase(entry.first);
                owned_decl_contexts.erase(to_overset);
            }
        }
        for(auto&& entry : from->decls) {
            if(auto overset = dynamic_cast<AST::FunctionOverloadSet*>(entry.second)) {
                auto to_overset = dynamic_cast<AST::FunctionOverloadSet*>(to->decls[entry.first]);
                assert(to_overset);
                assert(owned_decl_contexts.find(to_overset) != owned_decl_contexts.end());
                for(auto&& fun : overset->functions)
                    to_overset->functions.erase(fun);
                if (to_overset->functions.empty()) {
                    to->decls.erase(entry.first);
                    owned_decl_contexts.erase(to_overset);
                }
                continue;
            }
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
        }
    };
    remover(&root, m);
}