#include <Wide/CAPI/Parser.h>
#include <Wide/Util/DebugUtilities.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    enum class DeclType : int {
        Function,
        Using,
        Type,
        Module,
    };
    struct CombinedError {
        CombinedError(CEquivalents::Range f, DeclType s)
            : where(std::move(f)), type(s) {}
        CEquivalents::Range where;
        DeclType type;
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
    std::add_pointer<void(CEquivalents::Range, Wide::Parse::OutliningType, void*)>::type OutliningCallback,
    std::add_pointer<void(unsigned count, CEquivalents::Range*, Wide::Parse::Error, void*)>::type ErrorCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parse::Warning, void*)>::type WarningCallback,
    const char* filename
) {
    auto onerror = [=](const std::vector<Wide::Lexer::Range>& where, Wide::Parse::Error what) {
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
    ret->builder.error = [=](std::vector<Wide::Lexer::Range> where, Wide::Parse::Error what) { onerror(where, what); };
    ret->builder.warning = [=](Wide::Lexer::Range where, Wide::Parse::Warning what) { return WarningCallback(where, what, context); };
    ret->builder.outlining = [=](Wide::Lexer::Range r, Wide::Parse::OutliningType t) { return OutliningCallback(r, t, context); };
    try {
        ret->builder.ParseGlobalModuleContents(&ret->builder.GlobalModule);
    } catch(Wide::Parse::ParserError& e) {
        onerror(e.where(), e.error());
    } catch(...) {
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

extern "C" DLLEXPORT const char* GetParserErrorString(Wide::Parse::Error err) {
    if (Wide::Parse::ErrorStrings.find(err) == Wide::Parse::ErrorStrings.end())
        return "ICE: Failed to retrieve error string: unknown error.";
    return Wide::Parse::ErrorStrings.at(err).c_str();
}

extern "C" DLLEXPORT const char* GetParserWarningString(Wide::Parse::Warning err) {
    if (Wide::Parse::WarningStrings.find(err) == Wide::Parse::WarningStrings.end())
        return "ICE: Failed to retrieve warning string: unknown warning.";
    return Wide::Parse::WarningStrings.at(err).c_str();
}
