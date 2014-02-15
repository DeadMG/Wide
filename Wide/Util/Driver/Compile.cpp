#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/Concurrency/ParallelForEach.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Parser/Builder.h>
#include <Wide/Util/Concurrency/ConcurrentVector.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Util/Ranges/IStreamRange.h>
#include <Wide/Codegen/Generator.h>
#include <mutex>
#include <atomic>
#include <sstream>
#include <fstream>
#include <memory>
#include <iostream>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/raw_os_ostream.h>
#pragma warning(pop)

void Wide::Driver::Compile(const Wide::Options::Clang& copts, std::function<void(Semantic::Analyzer&, const AST::Module*)> func, Codegen::Generator& gen, std::initializer_list<std::string> files) {
    return Compile(copts, std::move(func), gen, std::vector<std::string>(files.begin(), files.end()));
}
void Wide::Driver::Compile(const Wide::Options::Clang& copts, std::function<void(Wide::Semantic::Analyzer&, const AST::Module*)> func, Wide::Codegen::Generator& gen, const std::vector<std::string>& files) {
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
            Wide::Parser::AssumeLexer<decltype(lex)> lexer;
            lexer.lex = &lex;
            Wide::Parser::Parser<decltype(lexer), decltype(*builder)> parser(lexer, *builder);
            parser.ParseGlobalModuleContents(builder->GetGlobalModule());
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

    for(auto&& x : builders)
        combiner.Add(x->GetGlobalModule());

    if (excepts.empty()) {
        Wide::Semantic::Analyzer a(copts, gen, combiner.GetGlobalModule());
        func(a, combiner.GetGlobalModule());
        gen();
    } else {
        std::string err = "Compilation failed with errors:\n";
        for(auto&& msg : excepts) {
            err += "    " + msg + "\n";
        }
        throw std::runtime_error(err);
    }
}