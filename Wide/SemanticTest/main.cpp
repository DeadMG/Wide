#include <Wide/SemanticTest/MockCodeGenerator.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/ParallelForEach.h>
#include <Wide/Parser/Builder.h>
#include <Wide/SemanticTest/Test.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Util/Ranges/IStreamRange.h>
#include <Wide/Util/ConcurrentVector.h>
#include <mutex>
#include <atomic>
#include <sstream>
#include <fstream>
#include <initializer_list>
#include <iostream>

#ifndef _MSC_VER
#include <llvm/Support/Host.h>
#endif

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Catch.h>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

void Compile(const Wide::Options::Clang& copts, llvm::DataLayout lopts, std::initializer_list<std::string> files) {
    Wide::Codegen::MockGenerator mockgen(lopts);
    
    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::Vector<std::string> warnings;
    auto parsererrorhandler = [&](std::vector<Wide::Lexer::Range> where, Wide::Parser::Error what) {
        std::stringstream str;
        str << "Error at locations:\n";
        for(auto loc : where)
            str << "    File: " << *loc.begin.name << ", line: " << loc.begin.line << " column: " << loc.begin.line << "\n";
        str << Wide::Parser::ErrorStrings.at(what);
        excepts.push_back(str.str());
    };
    auto combineerrorhandler = [=](std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>> errs) {
    };
    Wide::AST::Combiner combiner(combineerrorhandler);
    Wide::Concurrency::Vector<std::shared_ptr<Wide::AST::Builder>> builders;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = Wide::Range::IStreamRange(inputfile);
        Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents, std::make_shared<std::string>(filename));
        auto parserwarninghandler = [&](Wide::Lexer::Range where, Wide::Parser::Warning what) {
            std::stringstream str;
            str << "Warning in file " << filename << ", line " << where.begin.line << " column " << where.begin.column << ":\n";
            str << Wide::Parser::WarningStrings.at(what);
            warnings.push_back(str.str());
        };
        try {
            auto builder = std::make_shared<Wide::AST::Builder>(parsererrorhandler, parserwarninghandler, [](Wide::Lexer::Range, Wide::AST::OutliningType){});
            Wide::Parser::ParseGlobalModuleContents(lex, *builder, builder->GetGlobalModule());
            builders.push_back(std::move(builder));
        } catch(Wide::Parser::ParserError& e) {
            parsererrorhandler(e.where(), e.error());
        } catch(std::exception& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Internal Compiler Error");
        }
    });

    for(auto&& x : warnings)
        std::cout << x << "\n";
    
    for (auto&& x : warnings)
        std::cout << x << "\n";

    if (excepts.empty()) {
        for (auto&& x : builders)
            combiner.Add(x->GetGlobalModule());
        Wide::Semantic::Analyzer sema(copts, &mockgen, combiner.GetGlobalModule());
        Test(sema, nullptr, combiner.GetGlobalModule(), [&](Wide::Lexer::Range r, Wide::Semantic::Error e) { throw Wide::Semantic::SemanticError(r, e); }, mockgen, false);
    } else {
        for (auto&& msg : excepts) {
            std::cout << msg << "\n";
        }
        throw std::runtime_error("Terminating test due to failures.");
    }
}

TEST_CASE("", "") {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(clangopts.TargetOptions.Triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(clangopts.TargetOptions.Triple, llvm::Triple(clangopts.TargetOptions.Triple).getArchName(), "", targetopts));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "IntegerOperations.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "PrimitiveADL.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "RecursiveTypeInference.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "CorecursiveTypeInference.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "MemberCall.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "AcceptQualifiedThis.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "DeferredVariable.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "DeferredLambda.wide" }));
    CHECK_THROWS(Compile(clangopts, *targetmachine->getDataLayout(), { "RejectQualifiedThis.wide" }));
    CHECK_THROWS(Compile(clangopts, *targetmachine->getDataLayout(), { "SubmoduleNoQualifiedLookup.wide" }));
}