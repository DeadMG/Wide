#include <Wide/CAPI/Parser.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    struct Combiner;
    struct Builder {
        std::unordered_set<Combiner*> combiners;
        Wide::Parse::Parser builder;
    };
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
    CEquivalents::Builder* ret = new CEquivalents::Builder{
        {},
        { 
            [TokenCallback, context, filename]() -> Wide::Util::optional<Wide::Lexer::Token> { 
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
            }
        }
    };
    try {
        ret->builder.ParseGlobalModuleContents(&ret->builder.GlobalModule);
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
    for (auto comb : p->combiners) {
        comb->combiner.Remove(&p->builder.GlobalModule);
        comb->builders.erase(p);
    }
    delete p;
}

extern "C" DLLEXPORT CEquivalents::Combiner* CreateCombiner() {
    return new CEquivalents::Combiner();
}

extern "C" DLLEXPORT void DestroyCombiner(CEquivalents::Combiner* p) {
    for (auto builder : p->builders)
        builder->combiners.erase(p);
    delete p;
}

extern "C" DLLEXPORT void AddParser(CEquivalents::Combiner* c, CEquivalents::Builder** p, unsigned count) {
    c->builders.clear();
    c->builders.insert(p, p + count);
    std::unordered_set<Wide::Parse::Module*> modules;
    for (auto&& x : c->builders) {
        x->combiners.insert(c);
        modules.insert(&x->builder.GlobalModule);
    }
    c->combiner.SetModules(std::move(modules));
}

extern "C" DLLEXPORT void GetOutlining(
    CEquivalents::Builder* p, 
    std::add_pointer<void(CEquivalents::Range, void*)>::type OutliningCallback,
    void* context
) {

}