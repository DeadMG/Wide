#include <Wide/CAPI/Parser.h>
#include <Wide/Util/DebugUtilities.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    struct ParserLexer {
        ParserLexer(std::shared_ptr<std::string> where)
            : lastpos(where) {}
        void* context;
        std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback;
        Wide::Lexer::Range lastpos;
        std::deque<Wide::Lexer::Token> putback;
        Wide::Util::optional<Wide::Lexer::Token> operator()() {
            if (!putback.empty()) {
                auto val = putback.back();
                putback.pop_back();
                lastpos = val.GetLocation();
                return std::move(val);
            }
            auto tok = TokenCallback(context);
            if (tok.exists) {
                lastpos.begin.line = tok.location.begin.line;
                lastpos.begin.column = tok.location.begin.column;
                lastpos.begin.offset = tok.location.begin.offset;
                lastpos.end.line = tok.location.end.line;
                lastpos.end.column = tok.location.end.column;
                lastpos.end.offset = tok.location.end.offset;
                return Wide::Lexer::Token(lastpos, tok.type, tok.value);
            }
            return Wide::Util::none;
        }
        void operator()(Wide::Lexer::Token t) {
            putback.push_back(std::move(t));
        }
        Wide::Lexer::Range GetLastPosition() {
            return lastpos;
        }
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
        Wide::AST::Builder builder;
    };
}

extern "C" DLLEXPORT CEquivalents::Builder* ParseWide(
    void* context,
    std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::AST::OutliningType, void*)>::type OutliningCallback,
    std::add_pointer<void(unsigned count, CEquivalents::Range*, Wide::Parser::Error, void*)>::type ErrorCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parser::Warning, void*)>::type WarningCallback,
    const char* filename
) {
    auto onerror = [=](const std::vector<Wide::Lexer::Range>& where, Wide::Parser::Error what) {
        std::vector<CEquivalents::Range> locs;
        for(auto x : where)
            locs.push_back(CEquivalents::Range(x));
        ErrorCallback(locs.size(), locs.data(), what, context);
    };
    CEquivalents::Builder* ret = new CEquivalents::Builder{
        {},
        {
            [=](std::vector<Wide::Lexer::Range> where, Wide::Parser::Error what) { onerror(where, what); },
            [=](Wide::Lexer::Range where, Wide::Parser::Warning what) { return WarningCallback(where, what, context); },
            [=](Wide::Lexer::Range r, Wide::AST::OutliningType t) { return OutliningCallback(r, t, context); }
        }
    };
    CEquivalents::ParserLexer pl(std::make_shared<std::string>(filename));
    Wide::Parser::AssumeLexer<decltype(pl)> lexer;
    lexer.lex = &pl;
    Wide::Parser::Parser<decltype(lexer), decltype(ret->builder)> parser(lexer, ret->builder);
    pl.context = context;
    pl.TokenCallback = TokenCallback;
    try {
        parser.ParseGlobalModuleContents(ret->builder.GetGlobalModule());
    } catch(Wide::Parser::ParserError& e) {
        onerror(e.where(), e.error());
    } catch(...) {
    }
    return ret;
}

extern "C" DLLEXPORT void DestroyParser(CEquivalents::Builder* p) {
    for (auto comb : p->combiners) {
        comb->combiner.Remove(p->builder.GetGlobalModule());
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
    std::unordered_set<Wide::AST::Module*> modules;
    for (auto&& x : c->builders) {
        x->combiners.insert(c);
        modules.insert(x->builder.GetGlobalModule());
    }
    c->combiner.SetModules(std::move(modules));
}

extern "C" DLLEXPORT void GetCombinerErrors(
    CEquivalents::Combiner* c,
    std::add_pointer<void(unsigned count, CEquivalents::CombinedError*, void*)>::type ErrorCallback,
    void* context
) {
    std::vector<CEquivalents::CombinedError> err;
    err.reserve(10);
    auto onerror = [=](std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>& errs) mutable {
        for (auto error : errs) {
            CEquivalents::DeclType d;
            if (dynamic_cast<Wide::AST::Module*>(error.second))
                d = CEquivalents::DeclType::Module;
            if (dynamic_cast<Wide::AST::Function*>(error.second))
                d = CEquivalents::DeclType::Function;
            if (dynamic_cast<Wide::AST::Type*>(error.second))
                d = CEquivalents::DeclType::Type;
            if (dynamic_cast<Wide::AST::Using*>(error.second))
                d = CEquivalents::DeclType::Using;
            if (auto overset = dynamic_cast<Wide::AST::FunctionOverloadSet*>(error.second)) {
                for (auto f : overset->functions)
                    err.push_back(CEquivalents::CombinedError(f->where, CEquivalents::DeclType::Function));
            }
            else
                err.push_back(CEquivalents::CombinedError(*error.first, d));
        }
        ErrorCallback(err.size(), err.data(), context);
        err.clear();
    };
    c->combiner.ReportErrors(onerror);
}

extern "C" DLLEXPORT const char* GetParserErrorString(Wide::Parser::Error err) {
    if (Wide::Parser::ErrorStrings.find(err) == Wide::Parser::ErrorStrings.end())
        return "ICE: Failed to retrieve error string: unknown error.";
    return Wide::Parser::ErrorStrings.at(err).c_str();
}

extern "C" DLLEXPORT const char* GetParserWarningString(Wide::Parser::Warning err) {
    if (Wide::Parser::WarningStrings.find(err) == Wide::Parser::WarningStrings.end())
        return "ICE: Failed to retrieve warning string: unknown warning.";
    return Wide::Parser::WarningStrings.at(err).c_str();
}

extern "C" DLLEXPORT void GetParserCombinedErrors(
    CEquivalents::Builder* b,
    std::add_pointer<void(unsigned count, CEquivalents::CombinedError*, void*)>::type callback,
    void* context
) {
    auto errs = b->builder.GetCombinerErrors();
    for(auto x : errs) {
        std::vector<CEquivalents::CombinedError> err;
        for(auto error : x) {
            CEquivalents::DeclType d;
            if (dynamic_cast<Wide::AST::Module*>(error.second))
                d = CEquivalents::DeclType::Module;
            if (dynamic_cast<Wide::AST::Type*>(error.second))
                d = CEquivalents::DeclType::Type;
            if (dynamic_cast<Wide::AST::Using*>(error.second))
                d = CEquivalents::DeclType::Using;
            if (auto overset = dynamic_cast<Wide::AST::FunctionOverloadSet*>(error.second)) {
                for (auto f : overset->functions)
                    err.push_back(CEquivalents::CombinedError(error.first, CEquivalents::DeclType::Function));
            } else
                err.push_back(CEquivalents::CombinedError(error.first, d));
        }
        callback(err.size(), err.data(), context);
    }
}
