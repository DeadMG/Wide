#pragma once

#include <Wide/Util/Driver/Options.h>
#include <vector>
#include <memory>
#include <Wide/Parser/AST.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Lexer/LexerError.h>
#include <Wide/Semantic/SemanticError.h>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Driver {
        struct LexResponse {
            std::vector<std::vector<Lexer::Token>> Tokens;
            std::vector<Lexer::Error> LexErrors;
        };
        struct ParseResponse {
            std::vector<std::shared_ptr<Parse::Module>> Modules;
            std::vector<Parse::Error> ParseErrors;
            std::vector<std::runtime_error> ASTErrors;
        };
        struct CombineResponse {
            std::shared_ptr<Parse::Module> CombinedModule;
            std::vector<std::runtime_error> ASTErrors;
        };
        struct AnalysisResponse {
            std::vector<Semantic::Error> AnalysisErrors;
            std::vector<Semantic::ClangDiagnostic> ClangDiagnostics;
            std::unique_ptr<llvm::LLVMContext> Context;
            std::unique_ptr<llvm::Module> Module;
            std::unique_ptr<std::runtime_error> OldError;
        };
        struct Response {
            LexResponse Lex;
            ParseResponse Parse;
            CombineResponse Combine;
            AnalysisResponse Analysis;
        };
        LexResponse Lex(std::vector<std::pair<std::string, std::string>>);
        ParseResponse Parse(std::vector<std::vector<Lexer::Token>>);
        CombineResponse Combine(std::vector<std::shared_ptr<Parse::Module>>);
        AnalysisResponse Analyse(const Parse::Module*, const Options::Clang&);
        AnalysisResponse Analyse(const Parse::Module*, const Options::Clang&, std::function<void(Wide::Semantic::Analyzer& a)>);
        Options::Clang PrepareClangOptions(DriverOptions);
        Response Execute(DriverOptions);
        bool Failed(const Response&);
    }
}