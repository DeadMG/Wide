#include <Wide/Parser/AST.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <functional>
#include <cassert>
#include <boost/variant/get.hpp>

using namespace Wide;
using namespace Parse;

void Module::unify(const Module& with) {
    std::unordered_set<std::string> inserted_shared;
    std::unordered_map<std::string, const Parse::UniqueAccessContainer*> unified_unique;
    std::unordered_map<std::string, const Parse::MultipleAccessContainer*> unified_multiple;

    try {
        for (auto&& decl : with.named_decls) {
            if (auto&& shared = boost::get<std::pair<Parse::Access, std::shared_ptr<SharedObjectTag>>>(&decl.second)) {
                if (named_decls.find(decl.first) == named_decls.end()) {
                    named_decls.insert(std::make_pair(decl.first, *shared));
                    inserted_shared.insert(decl.first);
                } else
                    throw std::runtime_error("Bad AST.");
            }
            if (auto&& unique = boost::get<std::pair<Parse::Access, std::unique_ptr<Parse::UniqueAccessContainer>>>(&decl.second)) {
                if (named_decls.find(decl.first) == named_decls.end()) {
                    named_decls.insert(std::make_pair(decl.first, std::make_pair(unique->first, unique->second->clone())));
                    unified_unique.insert(std::make_pair(decl.first, unique->second.get()));
                } else if (auto&& mymulti = boost::get<std::pair<Parse::Access, std::unique_ptr<Parse::UniqueAccessContainer>>>(&named_decls[decl.first])) {
                    mymulti->second->unify(*unique->second);
                    unified_unique.insert(std::make_pair(decl.first, unique->second.get()));
                } else
                    throw std::runtime_error("Bad AST.");
            }
            if (auto&& multi = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(&decl.second)) {
                if (named_decls.find(decl.first) == named_decls.end()) {
                    named_decls.insert(std::make_pair(decl.first, (*multi)->clone()));
                    unified_multiple.insert(std::make_pair(decl.first, (*multi).get()));
                } else if (auto&& mymulti = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(&named_decls[decl.first])) {
                    (*mymulti)->unify(**multi);
                    unified_multiple.insert(std::make_pair(decl.first, (*multi).get()));
                } else
                    throw std::runtime_error("Bad AST.");
            }
        }
    }
    catch (...) {
        for (auto&& name : inserted_shared)
            named_decls.erase(name);
        for (auto&& unique : unified_unique) {
            auto&& my_unique = boost::get<std::pair<Parse::Access, std::unique_ptr<Parse::UniqueAccessContainer>>>(named_decls[unique.first]);
            if (my_unique.second->remove(*unique.second))
                named_decls.erase(unique.first);
        }
        for (auto&& multi : unified_multiple) {
            auto&& my_multi = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(named_decls[multi.first]);
            if (my_multi->remove(*multi.second))
                named_decls.erase(multi.first);
        }
        throw;
    }

    // All nothrow now.    
    constructor_decls.insert(with.constructor_decls.begin(), with.constructor_decls.end());
    destructor_decls.insert(with.destructor_decls.begin(), with.destructor_decls.end());
    locations.insert(with.locations.begin(), with.locations.end());

    for (auto&& name : with.OperatorOverloads) {
        for (auto&& access : name.second) {
            for (auto&& op : access.second) {
                OperatorOverloads[name.first][access.first].insert(op);
            }
        }
    }
}
bool Module::remove(const Module& with) {
    for (auto&& decl : with.constructor_decls)
        constructor_decls.erase(decl);
    for (auto&& decl : with.destructor_decls)
        destructor_decls.erase(decl);
    for (auto&& loc : with.locations)
        locations.erase(loc);

    for (auto&& name : with.OperatorOverloads) {
        for (auto&& access : name.second) {
            for (auto&& op : access.second) {
                OperatorOverloads[name.first][access.first].erase(op);
                // Also remove any empty sets.
                if (OperatorOverloads[name.first][access.first].empty())
                    OperatorOverloads[name.first].erase(access.first);
                if (OperatorOverloads[name.first].empty())
                    OperatorOverloads.erase(name.first);
            }
        }
    }

    for (auto&& decl : with.named_decls) {
        if (boost::get<std::pair<Parse::Access, std::shared_ptr<SharedObjectTag>>>(&decl.second))
            named_decls.erase(decl.first);
        if (auto&& single = boost::get<std::pair<Parse::Access, std::unique_ptr<UniqueAccessContainer>>>(&decl.second))
            if (boost::get<std::pair<Parse::Access, std::unique_ptr<UniqueAccessContainer>>>(named_decls[decl.first]).second->remove(*single->second))
                named_decls.erase(decl.first);
        if (auto&& single = boost::get<std::unique_ptr<MultipleAccessContainer>>(&decl.second))
            if (boost::get<std::unique_ptr<MultipleAccessContainer>>(named_decls[decl.first])->remove(**single))
                named_decls.erase(decl.first);
    }

    return
        OperatorOverloads.empty() &&
        named_decls.empty() &&
        constructor_decls.empty() &&
        destructor_decls.empty();
}

void Combiner::Add(Module* mod) {
    assert(modules.find(mod) == modules.end());
    modules.insert(mod);
    root.unify(*mod);
}
void Combiner::Remove(Module* mod) {
    assert(modules.find(mod) != modules.end());
    modules.erase(mod);
    root.remove(*mod);
}
void Combiner::SetModules(std::unordered_set<Module*> mods) {
    std::vector<Module*> removals;
    for (auto&& mod : modules) {
        if (mods.find(mod) == mods.end())
            removals.push_back(mod);
    }
    for (auto&& mod : removals)
        Remove(mod);
    for (auto&& mod : mods) {
        if (modules.find(mod) == modules.end())
            Add(mod);
    }
}