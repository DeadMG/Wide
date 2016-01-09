#include <Wide/Util/Concurrency/ParallelForEach.h>
#include <Wide/Util/Concurrency/ConcurrentVector.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Util/Driver/IncludePaths.h>
#include <boost/program_options.hpp>
#include <memory>
#include <iterator>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Util/Driver/StdlibDirectorySearch.h>
#include <Wide/Util/Paths/Exists.h>
#include <Wide/Util/Driver/Execute.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Ranges/StringRange.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Semantic/Analyzer.h>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#pragma warning(pop)

// Don't using namespace Wide.
// https://connect.microsoft.com/VisualStudio/feedback/details/2212996/unqualified-reference-to-concurrency-breaks-compilation-in-ppl-h
using namespace Wide::Driver;

LexResponse Wide::Driver::Lex(std::vector<std::pair<std::string, std::string>> sources) {
    Wide::Concurrency::Vector<std::vector<Wide::Lexer::Token>> tokens;
    Wide::Concurrency::Vector<Lexer::Error> errors;
    Wide::Concurrency::ParallelForEach(sources.begin(), sources.end(), [&](const std::pair<std::string, std::string>& source) {
        std::vector<Wide::Lexer::Token> file_tokens;
        Wide::Lexer::Invocation lex(Wide::Range::StringRange(source.first), std::make_shared<std::string>(source.second));
        lex.OnError = [&](Lexer::Error error) {
            errors.push_back(error);
            return lex();
        };
        while (auto tok = lex())
            file_tokens.push_back(*tok);
    });
    return LexResponse{
        std::vector<std::vector<Wide::Lexer::Token>>(tokens.begin(), tokens.end()),
        std::vector<Lexer::Error>(errors.begin(), errors.end())
    };
}

ParseResponse Wide::Driver::Parse(std::vector<std::vector<Lexer::Token>> tokens) {
    Wide::Concurrency::Vector<std::shared_ptr<Parse::Module>> Modules;
    Wide::Concurrency::Vector<Parse::Error> ParseErrors;
    Wide::Concurrency::Vector<std::runtime_error> ASTErrors;
    Wide::Concurrency::ParallelForEach(tokens.begin(), tokens.end(), [&](const std::vector<Lexer::Token>& source) {
        auto tokens = Wide::Range::IteratorRange(source.begin(), source.end());
        try {
            auto parser = std::make_shared<Wide::Parse::Parser>(tokens);
            auto mod = Wide::Memory::MakeUnique<Wide::Parse::Module>(Wide::Util::none);
            auto results = parser->ParseGlobalModuleContents();
            for (auto&& member : results)
                parser->AddMemberToModule(mod.get(), std::move(member));
            Modules.push_back(std::move(mod));
        }
        catch (Wide::Parse::Error& e) {
            ParseErrors.push_back(e);
        }
        catch (std::runtime_error& e) {
            ASTErrors.push_back(e);
        }        
    });
    return ParseResponse{
        std::vector<std::shared_ptr<Parse::Module>>(Modules.begin(), Modules.end()),
        std::vector<Parse::Error>(ParseErrors.begin(), ParseErrors.end()),
        std::vector<std::runtime_error>(ASTErrors.begin(), ASTErrors.end())
    };    
}

CombineResponse Wide::Driver::Combine(std::vector<std::shared_ptr<Parse::Module>> in) {
    Wide::Parse::Combiner combiner;
    std::vector<std::runtime_error> errors;
    for (auto&& x : in) {
        try {
            combiner.Add(x.get());
        } catch (std::runtime_error e) {
            errors.push_back(e);
        }
    }
    return CombineResponse{
        combiner.GetGlobalModule(),
        std::move(errors)
    };
}

AnalysisResponse Wide::Driver::Analyse(const Parse::Module* mod, const Options::Clang& clangopts, std::function<void(Wide::Semantic::Analyzer& a)> func) {
    auto context = std::make_unique<llvm::LLVMContext>();
    Wide::Semantic::Analyzer a(clangopts, mod, *context);
    try {
        func(a);
    } catch (Wide::Semantic::Error& err) {
        AnalysisResponse r;
        r.AnalysisErrors.push_back(err);
        return r;
    } catch (std::runtime_error& err) {
        AnalysisResponse r;
        r.OldError = std::make_unique<std::runtime_error>(err);
        return r;
    }
    auto module = Wide::Util::CreateModuleForTriple(clangopts.TargetOptions.Triple, *context);
    a.GenerateCode(module.get());
    AnalysisResponse response;
    for (auto&& err : a.errors) {
        response.AnalysisErrors.push_back(*err);
    }
    response.ClangDiagnostics = a.ClangDiagnostics;
    response.Context = std::move(context);
    response.Module = std::move(module);
    return response;
}

AnalysisResponse Wide::Driver::Analyse(const Parse::Module* mod, const Options::Clang& clangopts) {
    return Wide::Driver::Analyse(mod, clangopts, [](Wide::Semantic::Analyzer& a) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
    });
}

Response Wide::Driver::Execute(DriverOptions driveropts) {
    Response r;
    r.Lex = Lex(driveropts.WideInputFiles);
    if (Failed(r))
        return r;
    r.Parse = Parse(r.Lex.Tokens);
    if (Failed(r))
        return r;
    r.Combine = Combine(r.Parse.Modules);
    if (Failed(r))
        return r;
    r.Analysis = Analyse(r.Combine.CombinedModule.get(), Wide::Driver::PrepareClangOptions(driveropts));
    return r;
}

Wide::Options::Clang Wide::Driver::PrepareClangOptions(DriverOptions opts) {
    Wide::Options::Clang ClangOpts;
    ClangOpts.FrontendOptions.OutputFile = opts.OutputFile;
    ClangOpts.TargetOptions.Triple = opts.TargetTriple;
    
    for (auto&& path : opts.CppIncludePaths) {
        ClangOpts.HeaderSearchOptions->AddPath(path, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    }

    return ClangOpts;
}
bool Wide::Driver::Failed(const Response& r) {
    auto errored = !r.Lex.LexErrors.empty()
        || !r.Parse.ParseErrors.empty()
        || !r.Parse.ASTErrors.empty()
        || !r.Combine.ASTErrors.empty()
        || !r.Analysis.AnalysisErrors.empty()
        || r.Analysis.OldError;
    if (errored) return errored;
    for (auto&& diag : r.Analysis.ClangDiagnostics) {
        if (diag.severity == Semantic::ClangDiagnostic::Severity::Error || diag.severity == Semantic::ClangDiagnostic::Severity::Fatal)
            return true;
    }
    return false;
}