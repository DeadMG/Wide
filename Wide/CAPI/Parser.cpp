#include <Wide/CAPI/Parser.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    struct Builder;
    struct Combiner {
        std::unordered_set<Builder*> builders;
        Wide::Parse::Combiner combiner;
        ~Combiner();
    };
    struct Builder {
        std::unordered_set<Combiner*> combiners;
        std::unique_ptr<Wide::Parse::Module> GlobalModule;
        ~Builder() {
            for (auto&& comb : combiners) {
                comb->combiner.Remove(GlobalModule.get());
                comb->builders.erase(this);
            }
        }
    };
}
CEquivalents::Combiner::~Combiner() {
    for (auto&& build : builders)
        build->combiners.erase(this);
}

extern "C" DLLEXPORT CEquivalents::Builder* ParseWide(
    void* context,
    std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback,
    std::add_pointer<void(unsigned count, CEquivalents::Range*, const char*, void*)>::type ErrorCallback,
    const char* filename
) {
    auto onerror = [=](const std::vector<Wide::Lexer::Range>& where, const char* what) {
        std::vector<CEquivalents::Range> locs;
        for(auto x : where)
            locs.push_back(CEquivalents::Range(x));
        ErrorCallback(locs.size(), locs.data(), what, context);
    };
    auto ret = new CEquivalents::Builder();
    ret->GlobalModule = Wide::Memory::MakeUnique<Wide::Parse::Module>(Wide::Util::none);
    Wide::Parse::Parser p([TokenCallback, context, filename]() -> Wide::Util::optional<Wide::Lexer::Token> {
        auto tok = TokenCallback(context);
        if (!tok.exists) return Wide::Util::none;
        auto str = std::make_shared<std::string>(filename);
        Wide::Lexer::Position begin(str);
        begin.line = tok.location.begin.line;
        begin.column = tok.location.begin.column;
        begin.offset = tok.location.begin.offset;
        Wide::Lexer::Position end(str);
        end.line = tok.location.begin.line;
        end.column = tok.location.begin.column;
        end.offset = tok.location.end.offset;
        return Wide::Lexer::Token(Wide::Lexer::Range(begin, end), tok.type, tok.value);
    });
    try {
        auto contents = p.ParseGlobalModuleContents(nullptr);
        for (auto&& member : contents)
            p.AddMemberToModule(ret->GlobalModule.get(), std::move(member));
    } catch(Wide::Parse::Error& e) {
        if (auto tok = e.GetInvalidToken())
            onerror({ tok->GetLocation() }, e.what());
        else
            onerror({ e.GetLastValidToken().GetLocation() }, e.what());
    } catch(std::runtime_error& e) {

    }
    return ret;
}

extern "C" DLLEXPORT void DestroyParser(CEquivalents::Builder* p) {
    delete p;
}

extern "C" DLLEXPORT CEquivalents::Combiner* CreateCombiner() {
    return new CEquivalents::Combiner();
}

extern "C" DLLEXPORT void DestroyCombiner(CEquivalents::Combiner* p) {
    delete p;
}

extern "C" DLLEXPORT void AddParser(CEquivalents::Combiner* c, CEquivalents::Builder** p, unsigned count) {
    c->builders.clear();
    c->builders.insert(p, p + count);
    std::unordered_set<Wide::Parse::Module*> modules;
    for (auto&& x : c->builders) {
        x->combiners.insert(c);
        modules.insert(x->GlobalModule.get());
    }
    c->combiner.SetModules(std::move(modules));
}

struct OutliningContext {
    std::unordered_map<std::type_index, std::function<void(const void*, const OutliningContext&)>> OutliningHandlers;
    std::function<void(Wide::Lexer::Range)> OutlineCallback;
    template<typename T, typename F> void AddHandler(F f) {
        OutliningHandlers[typeid(T)] = [f](const void* farg, const OutliningContext& con) {
            return f(static_cast<T*>(farg), con);
        };
    }
    template<typename T> void Outline(const T* p) const {
        auto&& id = typeid(*p);
        if (OutliningHandlers.find(id) != OutliningHandlers.end())
            OutliningHandlers.at(id)(p, *this);
    }
    template<typename T> void Outline(const std::shared_ptr<T>& p) const {
        Outline(p.get());
    }
    template<typename T> void Outline(const std::unique_ptr<T>& p) const {
        Outline(p.get());
    }
};
extern "C" DLLEXPORT void GetOutlining(
    CEquivalents::Builder* p, 
    std::add_pointer<void(CEquivalents::Range, void*)>::type OutliningCallback,
    void* context
) {
    OutliningContext con;
    con.OutlineCallback = [&](Wide::Lexer::Range range) {
        OutliningCallback(range, context);
    };
    con.AddHandler<const Wide::Parse::Module>([](const Wide::Parse::Module* p, const OutliningContext& con) {
        for (auto&& cons : p->constructor_decls) {
            con.Outline(cons);
        }
        for (auto&& des : p->destructor_decls) {
            con.Outline(des);
        }
        for (auto&& op : p->OperatorOverloads) {
            for (auto&& access : op.second) {
                for (auto&& func : access.second) {
                    con.Outline(func);
                }
            }
        }
        for (auto&& name : p->named_decls) {
            if (auto&& unique = boost::get<std::pair<Wide::Parse::Access, std::unique_ptr<Wide::Parse::UniqueAccessContainer>>>(&name.second)) {
                con.Outline(unique->second);
            }
            if (auto&& shared = boost::get<std::pair<Wide::Parse::Access, std::shared_ptr<Wide::Parse::SharedObject>>>(&name.second))
                con.Outline(shared->second);
            if (auto&& multi = boost::get<std::unique_ptr<Wide::Parse::MultipleAccessContainer>>(&name.second))
                con.Outline(multi);
        }
        for (auto&& loc : p->locations)
            if (auto&& longform = boost::get<Wide::Parse::ModuleLocation::LongForm>(&loc.location))
                con.OutlineCallback(longform->OpenCurly + longform->CloseCurly);
    });
    con.Outline(p->GlobalModule.get());
}
