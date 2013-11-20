#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Parser/Builder.h>
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
}

std::unordered_set<Wide::AST::Builder*> valid_builders;

extern "C" DLLEXPORT Wide::AST::Builder* ParseWide(
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
    Wide::AST::Builder& builder = *new Wide::AST::Builder(
         [=](std::vector<Wide::Lexer::Range> where, Wide::Parser::Error what) { onerror(where, what); },
         [=](Wide::Lexer::Range where, Wide::Parser::Warning what) { return WarningCallback(where, what, context); },
         [=](Wide::Lexer::Range r, Wide::AST::OutliningType t) { return OutliningCallback(r, t, context); }
    );
    CEquivalents::ParserLexer pl(std::make_shared<std::string>(filename));
    pl.context = context;
    pl.TokenCallback = TokenCallback;
    try {
        Wide::Parser::ParseGlobalModuleContents(pl, builder, builder.GetGlobalModule());
    } catch(Wide::Parser::ParserError& e) {
        onerror(e.where(), e.error());
    } catch(...) {
    }
    valid_builders.insert(&builder);
    return &builder;
}

extern "C" DLLEXPORT void DestroyParser(Wide::AST::Builder* p) {
    if (valid_builders.find(p) == valid_builders.end())
        Wide::Util::DebugBreak();
    valid_builders.erase(p);
    delete p;
}

extern "C" DLLEXPORT Wide::AST::Combiner* CreateCombiner(
    std::add_pointer<void(unsigned count, CEquivalents::CombinedError*, void*)>::type ErrorCallback,
    void* context
) {
    auto onerror = [=](std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>> errs) {
        std::vector<CEquivalents::CombinedError> err;
        for(auto error : errs) {
            CEquivalents::DeclType d;
            if (dynamic_cast<Wide::AST::Module*>(error.second))
                d = CEquivalents::DeclType::Module;
            if (dynamic_cast<Wide::AST::Function*>(error.second))
                d = CEquivalents::DeclType::Function;
            if (dynamic_cast<Wide::AST::Type*>(error.second))
                d = CEquivalents::DeclType::Type;
            if (dynamic_cast<Wide::AST::Using*>(error.second))
                d = CEquivalents::DeclType::Using;
            err.push_back(CEquivalents::CombinedError(error.first, d));
        }
        ErrorCallback(err.size(), err.data(), context);
    };
    return new Wide::AST::Combiner(onerror);
}

extern "C" DLLEXPORT void DestroyCombiner(Wide::AST::Combiner* p) {
    delete p;
}

extern "C" DLLEXPORT void AddParser(Wide::AST::Combiner* c, Wide::AST::Builder* p) {
    assert(valid_builders.find(p) != valid_builders.end());
    c->Add(p->GetGlobalModule());
}

extern "C" DLLEXPORT void RemoveParser(Wide::AST::Combiner* c, Wide::AST::Builder* p) {
    assert(valid_builders.find(p) != valid_builders.end());
    c->Remove(p->GetGlobalModule());
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
    Wide::AST::Builder* b,
    std::add_pointer<void(unsigned count, CEquivalents::CombinedError*, void*)>::type callback,
    void* context
) { 
    assert(valid_builders.find(b) != valid_builders.end());
    auto errs = b->GetCombinerErrors();
    for(auto x : errs) {
        std::vector<CEquivalents::CombinedError> err;
        for(auto error : x) {
            CEquivalents::DeclType d;
            if (dynamic_cast<Wide::AST::Module*>(error.second))
                d = CEquivalents::DeclType::Module;
            if (dynamic_cast<Wide::AST::Function*>(error.second))
                d = CEquivalents::DeclType::Function;
            if (dynamic_cast<Wide::AST::Type*>(error.second))
                d = CEquivalents::DeclType::Type;
            if (dynamic_cast<Wide::AST::Using*>(error.second))
                d = CEquivalents::DeclType::Using;
            err.push_back(CEquivalents::CombinedError(error.first, d));
        }
        callback(err.size(), err.data(), context);
    }
}
