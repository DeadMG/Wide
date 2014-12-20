#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/Concurrency/ParallelForEach.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Concurrency/ConcurrentVector.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Util/Ranges/IStreamRange.h>
#include <Wide/Util/Ranges/StringRange.h>
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

void Wide::Driver::Compile(const Wide::Options::Clang& copts, const std::vector<std::string>& files, llvm::LLVMContext& con, std::function<void(Semantic::Analyzer&, const Parse::Module*)> func) {
    return Wide::Driver::Compile(copts, files, con, {}, std::unordered_map<std::string, std::string>(), func);
}

void Wide::Driver::Compile(const Wide::Options::Clang& copts, const std::vector<std::string>& files, llvm::LLVMContext& con, const std::vector<std::pair<std::string, std::string>>& sources, std::function<void(Wide::Semantic::Analyzer&, const Parse::Module*)> func) {
    return Wide::Driver::Compile(copts, files, con, sources, std::unordered_map<std::string, std::string>(), func);
}
void Wide::Driver::Compile(const Wide::Options::Clang& copts,
    const std::vector<std::string>& files, 
    llvm::LLVMContext& con,
    const std::vector<std::pair<std::string, std::string>>& sources,
    const std::unordered_map<std::string, std::string>& import_headers,
    std::function<void(Wide::Semantic::Analyzer&, const Parse::Module*)> func
) {
    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::Vector<std::string> warnings;
    Wide::Parse::Combiner combiner;
    Wide::Concurrency::Vector<std::shared_ptr<Wide::Parse::Parser>> builders;
    auto errs = Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open input file " + filename + "\n");
        std::noskipws(inputfile);
        Wide::Lexer::Invocation lex(Wide::Range::IStreamRange(inputfile), std::make_shared<std::string>(filename));
        try {
            auto parser = std::make_shared<Wide::Parse::Parser>(lex);
            parser->ParseGlobalModuleContents(&parser->GlobalModule);
            builders.push_back(std::move(parser));
        } catch(Wide::Parse::Error& e) {
            excepts.push_back(e.what());
        } catch(std::exception& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Internal Compiler Error");
        }
    });
    auto sourceerrs = Wide::Concurrency::ParallelForEach(sources.begin(), sources.end(), [&](const std::pair<std::string, std::string>& source) {
        Wide::Lexer::Invocation lex(Wide::Range::StringRange(source.first), std::make_shared<std::string>(source.second));
        try {
            auto parser = std::make_shared<Wide::Parse::Parser>(lex);
            parser->ParseGlobalModuleContents(&parser->GlobalModule);
            builders.push_back(std::move(parser));
        }
        catch (Wide::Parse::Error& e) {
            excepts.push_back(e.what());
        }
        catch (std::exception& e) {
            excepts.push_back(e.what());
        }
        catch (...) {
            excepts.push_back("Internal Compiler Error");
        }
    });
    errs.insert(errs.end(), sourceerrs.begin(), sourceerrs.end());
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
        Wide::Semantic::Analyzer a(copts, combiner.GetGlobalModule(), con, import_headers);
        func(a, combiner.GetGlobalModule());
    } else {
        std::string err = "Compilation failed with errors:\n";
        for(auto&& msg : excepts) {
            err += "    " + msg + "\n";
        }
        throw std::runtime_error(err);
    }
}
