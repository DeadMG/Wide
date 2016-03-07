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
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/FunctionType.h>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Linker/Linker.h>
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
        tokens.push_back(file_tokens);
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
        auto parser = std::make_shared<Wide::Parse::Parser>(tokens);
        try {
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
        for (auto&& err : parser->lex.errors)
            ParseErrors.push_back(err);
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

AnalysisResponse Wide::Driver::Analyse(const Parse::Module* mod, const Options::Clang& clangopts, std::function<void(Wide::Semantic::Analyzer& a, llvm::Module* mod)> func) {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = Wide::Util::CreateModuleForTriple(clangopts.TargetOptions.Triple, *context);
    Wide::Semantic::Analyzer a(clangopts, mod, *context);
    try {
        func(a, module.get());
    } catch (Wide::Semantic::Error& err) {
        AnalysisResponse r;
        r.AnalysisErrors.push_back(err);
        return r;
    } catch (std::runtime_error& err) {
        AnalysisResponse r;
        r.OldError = std::make_unique<std::runtime_error>(err);
        return r;
    }
    AnalysisResponse response;
    for (auto&& err : a.errors) {
        response.AnalysisErrors.push_back(*err);
    }
    for (auto&& diag : a.ClangDiagnostics) {
        if (diag.severity == Wide::Semantic::ClangDiagnostic::Severity::Note)
            response.ClangNotes.push_back(diag);
        if (diag.severity == Wide::Semantic::ClangDiagnostic::Severity::Remark)
            response.ClangRemarks.push_back(diag);
        if (diag.severity == Wide::Semantic::ClangDiagnostic::Severity::Warning)
            response.ClangWarnings.push_back(diag);
        if (diag.severity == Wide::Semantic::ClangDiagnostic::Severity::Error)
            response.ClangErrors.push_back(diag);
        if (diag.severity == Wide::Semantic::ClangDiagnostic::Severity::Fatal)
            response.ClangFatals.push_back(diag);
    }
    response.Context = std::move(context);
    response.Module = std::move(module);
    return response;
}

AnalysisResponse Wide::Driver::Analyse(const Parse::Module* mod, const Options::Clang& clangopts) {
    return Wide::Driver::Analyse(mod, clangopts, [](Wide::Semantic::Analyzer& a, llvm::Module* m) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
    });
}

void AnalyseForLinking(Wide::Semantic::Analyzer& a, llvm::Module* mod, LinkOptions opts) {
    static const Wide::Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
    Wide::Semantic::Context c(Wide::Semantic::Location(a), location);
    auto global = a.GetGlobalModule()->BuildValueConstruction(Wide::Semantic::Expression::NoInstance(), {}, c);
    auto main = Wide::Semantic::Type::AccessMember(Wide::Semantic::Expression::NoInstance(), std::move(global), std::string("Main"), c);
    if (!main)
        throw std::runtime_error("Could not find Main in global namespace.");
    auto overset = dynamic_cast<Wide::Semantic::OverloadSet*>(main->GetType(Wide::Semantic::Expression::NoInstance())->Decay());
    if (!overset)
        throw std::runtime_error("Main in global namespace was not an overload set.");
    auto f = overset->Resolve({}, c.from);
    auto func = dynamic_cast<Wide::Semantic::Function*>(f);
    if (!func)
        throw std::runtime_error("Could not resolve a zero-arguments function from global Main overload set.");
    func->ComputeBody();
    if (!a.errors.empty())
        return;
    a.GenerateCode(mod);
    auto&& con = mod->getContext();
    // Create main trampoline.
    auto llvmfunc = llvm::Function::Create(llvm::FunctionType::get(llvm::IntegerType::get(con, 32), {}, false), llvm::GlobalValue::LinkageTypes::ExternalLinkage, "main", mod);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvmfunc->getContext(), "entry", llvmfunc);
    llvm::IRBuilder<> builder(entry);
    auto call = builder.CreateCall(func->EmitCode(mod));
    if (call->getType() == llvm::Type::getVoidTy(con))
        builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(con), (uint64_t)0, true));
    else if (call->getType() == llvm::Type::getInt64Ty(con))
        builder.CreateRet(builder.CreateTrunc(call, llvm::Type::getInt32Ty(con)));
    else if (call->getType() == llvm::Type::getInt8Ty(con))
        if (func->GetSignature()->GetReturnType() == a.GetBooleanType())
            builder.CreateRet(builder.CreateZExt(builder.CreateNot(builder.CreateTrunc(call, llvm::Type::getInt1Ty(con))), llvm::Type::getInt32Ty(con)));
        else 
            builder.CreateRet(builder.CreateZExt(call, llvm::Type::getInt32Ty(con)));
    else {
        if (call->getType() != llvm::Type::getInt32Ty(con))
            throw std::runtime_error("Main() did not return an int32, int64, or void.");
        builder.CreateRet(call);
    }
    if (llvm::verifyModule(*mod))
        throw std::runtime_error("Internal compiler error: An LLVM module failed verification.");
}

namespace {
    void AnalyseForExporting(Wide::Semantic::Analyzer& a, llvm::Module* mod, ExportOptions opts) {

    }

    void AnalyseForMode(Wide::Semantic::Analyzer& a, llvm::Module* mod, boost::variant<LinkOptions, ExportOptions> opts) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
        if (auto link = boost::get<LinkOptions>(&opts))
            AnalyseForLinking(a, mod, *link);
        else
            AnalyseForExporting(a, mod, boost::get<ExportOptions>(opts));
    }

    void WriteLinkOutput(Response& r, DriverOptions& driveropts) {
        std::string mod_ir;
        llvm::raw_string_ostream stream(mod_ir);
        r.Analysis.Module->print(stream, nullptr);
        stream.flush();
        llvm::PassManager pm;
        std::unique_ptr<llvm::TargetMachine> targetmachine;
        llvm::TargetOptions targetopts;
        std::string err;
        const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(driveropts.TargetTriple, err);
        targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(driveropts.TargetTriple, llvm::Triple(driveropts.TargetTriple).getArchName(), "", targetopts));
        std::ofstream file(driveropts.OutputFile, std::ios::trunc | std::ios::binary);
        llvm::raw_os_ostream out(file);
        llvm::formatted_raw_ostream format_out(out);
        targetmachine->addPassesToEmitFile(pm, format_out, llvm::TargetMachine::CodeGenFileType::CGFT_ObjectFile);
        pm.run(*r.Analysis.Module);
    }

    void WriteOutput(Response& r, DriverOptions& driveropts) {
        if (auto link = boost::get<LinkOptions>(&driveropts.Mode))
            WriteLinkOutput(r, driveropts);
    }
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
    r.Analysis = Analyse(r.Combine.CombinedModule.get(), Wide::Driver::PrepareClangOptions(driveropts), [&](Wide::Semantic::Analyzer& a, llvm::Module* mod) {
        for (auto&& path : driveropts.CppInputFiles)
            a.AggregateCPPHeader(path, Wide::Lexer::Range(std::make_shared<std::string>(path)));
        AnalyseForMode(a, mod, driveropts.Mode);
    });
    if (Failed(r))
        return r;
    WriteOutput(r, driveropts);
    return r;
}

Wide::Options::Clang Wide::Driver::PrepareClangOptions(DriverOptions opts) {
    Wide::Options::Clang ClangOpts;
    ClangOpts.FrontendOptions.OutputFile = opts.OutputFile;
    ClangOpts.TargetOptions.Triple = opts.TargetTriple;
    ClangOpts.LanguageOptions.CPlusPlus14 = true;
    ClangOpts.LanguageOptions.CPlusPlus1z = true;
    ClangOpts.LanguageOptions.MSCompatibilityVersion = 0;
    ClangOpts.LanguageOptions.MSVCCompat = 0;
    ClangOpts.LanguageOptions.MicrosoftExt = 0;
    ClangOpts.LanguageOptions.GNUMode = true;
    ClangOpts.LanguageOptions.GNUKeywords = true;
    ClangOpts.LanguageOptions.CPlusPlus11 = true;
    ClangOpts.LanguageOptions.CPlusPlus = true;
    ClangOpts.LanguageOptions.LaxVectorConversions = true;
    for (auto&& path : opts.CppIncludePaths) {
        ClangOpts.HeaderSearchOptions->AddPath(path, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    }

    return ClangOpts;
}
bool Wide::Driver::Failed(const Response& r) {
    return !r.Lex.LexErrors.empty()
        || !r.Parse.ParseErrors.empty()
        || !r.Parse.ASTErrors.empty()
        || !r.Combine.ASTErrors.empty()
        || !r.Analysis.AnalysisErrors.empty()
        || r.Analysis.OldError
        //|| !r.Analysis.ClangErrors.empty()
        || !r.Analysis.ClangFatals.empty();
}
