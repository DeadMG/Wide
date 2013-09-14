#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Parser/Builder.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    struct ParserLexer {
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
                lastpos = tok.location;
                return Wide::Lexer::Token(tok.location, tok.type, tok.value);
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

    enum OutliningType {
        Module,
        Function,
        Type
    };
    
    class Builder : public Wide::AST::Builder {
    public:     

        std::function<void(Wide::Lexer::Range r, CEquivalents::OutliningType type)> OutliningCallback;

        Builder(std::function<void(Wide::Lexer::Range, Wide::Parser::Error)> err, 
                std::function<void(Wide::Lexer::Range, Wide::Parser::Warning)> war,
                std::function<void(Wide::Lexer::Range, CEquivalents::OutliningType)> out) 
        : Wide::AST::Builder(std::move(err), std::move(war))
        , OutliningCallback(std::move(out)) {}

        void CreateFunction(
            std::string name, 
            std::vector<Wide::AST::Statement*> body, 
            std::vector<Wide::AST::Statement*> prolog, 
            Wide::Lexer::Range r, 
            Wide::AST::Module* p, 
            std::vector<Wide::AST::FunctionArgument> args, 
            std::vector<Wide::AST::VariableStatement*> caps
        ) { 
            OutliningCallback(r, OutliningType::Function); 
            return Wide::AST::Builder::CreateFunction(std::move(name), std::move(body), std::move(prolog), r, p, std::move(args), std::move(caps)); 
        }
        void CreateFunction(
            std::string name, 
            std::vector<Wide::AST::Statement*> body, 
            std::vector<Wide::AST::Statement*> prolog, 
            Wide::Lexer::Range r, 
            Wide::AST::Type* p, 
            std::vector<Wide::AST::FunctionArgument> args, 
            std::vector<Wide::AST::VariableStatement*> caps
        ) { 
            OutliningCallback(r, OutliningType::Function); 
            return Wide::AST::Builder::CreateFunction(std::move(name), std::move(body), std::move(prolog), r, p, std::move(args), std::move(caps)); 
        }

        void CreateOverloadedOperator(
            Wide::Lexer::TokenType name, 
            std::vector<Wide::AST::Statement*> body, 
            std::vector<Wide::AST::Statement*> prolog, 
            Wide::Lexer::Range r, 
            Wide::AST::Module* p, 
            std::vector<Wide::AST::FunctionArgument> args
        ) {
            OutliningCallback(r, OutliningType::Function);
            return Wide::AST::Builder::CreateOverloadedOperator(name, std::move(body), std::move(prolog), r, p, std::move(args));
        }
        void CreateOverloadedOperator(
            Wide::Lexer::TokenType name, 
            std::vector<Wide::AST::Statement*> body,
            std::vector<Wide::AST::Statement*> prolog, 
            Wide::Lexer::Range r, Wide::AST::Type* p,
            std::vector<Wide::AST::FunctionArgument> args
        )
        { 
            OutliningCallback(r, OutliningType::Function); 
            return Wide::AST::Builder::CreateOverloadedOperator(name, std::move(body), std::move(prolog), r, p, std::move(args));
        }

        void SetTypeEndLocation(Wide::Lexer::Range loc, Wide::AST::Type* t) { Wide::AST::Builder::SetTypeEndLocation(loc, t); OutliningCallback(t->location, OutliningType::Type); }
        void SetModuleEndLocation(Wide::AST::Module* m, Wide::Lexer::Range loc) { Wide::AST::Builder::SetModuleEndLocation(m, loc); OutliningCallback(loc, OutliningType::Module); }
    };
}

extern "C" __declspec(dllexport) void ParseWide(
    void* context,
    std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback,
    std::add_pointer<void(CEquivalents::Range, CEquivalents::OutliningType, void*)>::type OutliningCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parser::Error, void*)>::type ErrorCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parser::Warning, void*)>::type WarningCallback
) {
    CEquivalents::Builder builder(
         [=](Wide::Lexer::Range where, Wide::Parser::Error what) { return ErrorCallback(where, what, context); },
         [=](Wide::Lexer::Range where, Wide::Parser::Warning what) { return WarningCallback(where, what, context); },
         [=](Wide::Lexer::Range r, CEquivalents::OutliningType t) { return OutliningCallback(r, t, context); }
    );
    CEquivalents::ParserLexer pl;
    pl.context = context;
    pl.TokenCallback = TokenCallback;
    try {
        Wide::Parser::ParseGlobalModuleContents(pl, builder, builder.GetGlobalModule());
    } catch(Wide::Parser::ParserError& e) {
        ErrorCallback(e.where(), e.error(), context);
    } catch(...) {
    }
}

extern "C" __declspec(dllexport) const char* GetParserErrorString(Wide::Parser::Error err) {
    if (Wide::Parser::ErrorStrings.find(err) == Wide::Parser::ErrorStrings.end())
        return nullptr;
    return Wide::Parser::ErrorStrings.at(err).c_str();
}

extern "C" __declspec(dllexport) const char* GetParserWarningString(Wide::Parser::Warning err) {
    if (Wide::Parser::WarningStrings.find(err) == Wide::Parser::WarningStrings.end())
        return nullptr;
    return Wide::Parser::WarningStrings.at(err).c_str();
}