#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/Concurrency/ParallelForEach.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Concurrency/ConcurrentVector.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Util/Ranges/IStreamRange.h>
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

void Wide::Driver::Compile(const Wide::Options::Clang& copts, std::function<void(Wide::Semantic::Analyzer&, const Parse::Module*)> func, const std::vector<std::string>& files) {
    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::Vector<std::string> warnings;
    auto parsererrorhandler = [&](std::vector<Wide::Lexer::Range> where, Wide::Parse::Error what) {
        std::stringstream str;
        str << "Error at locations:\n";
        for(auto loc : where)
            str << "    File: " << *loc.begin.name << ", line: " << loc.begin.line << " column: " << loc.begin.line << "\n";
        str << Wide::Parse::ErrorStrings.at(what);
        excepts.push_back(str.str());
    };
    Wide::Parse::Combiner combiner;
    Wide::Concurrency::Vector<std::shared_ptr<Wide::Parse::Parser>> builders;
    auto errs = Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open input file " + filename + "\n");
        std::noskipws(inputfile);
        Wide::Lexer::Invocation lex(Wide::Range::IStreamRange(inputfile), std::make_shared<std::string>(filename));
        auto parserwarninghandler = [&](Wide::Lexer::Range where, Wide::Parse::Warning what) {
            std::stringstream str;
            str << "Warning in file " << filename << ", line " << where.begin.line << " column " << where.begin.column << ":\n";
            str << Wide::Parse::WarningStrings.at(what);
            warnings.push_back(str.str());
        };
        try {
            auto parser = std::make_shared<Wide::Parse::Parser>(lex);
            parser->ParseGlobalModuleContents(&parser->GlobalModule);
            builders.push_back(std::move(parser));
        } catch(Wide::Parse::ParserError& e) {
            parsererrorhandler(e.where(), e.error());
        } catch(std::exception& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Internal Compiler Error");
        }
    });
    if (!errs.empty()) {
        if (errs.size() == 1)
            std::rethrow_exception(errs[0]);
        for (auto&& x : errs) {
            try {
                std::rethrow_exception(x);
            } catch (std::exception& e) {
                std::cout << "Error:\n" << e.what() << "\n";
            } catch (...) {
                std::cout << "Internal Compiler Error\n";
            }
        }
        throw std::runtime_error("Multiple errors occurred.");
    }
    for(auto&& x : warnings)
        std::cout << x << "\n";

    for(auto&& x : builders)
        combiner.Add(&x->GlobalModule);

    if (excepts.empty()) {
        Wide::Semantic::Analyzer a(copts, combiner.GetGlobalModule());
        func(a, combiner.GetGlobalModule());
    } else {
        std::string err = "Compilation failed with errors:\n";
        for(auto&& msg : excepts) {
            err += "    " + msg + "\n";
        }
        throw std::runtime_error(err);
    }
}
