#include "Wide.h"
#include "Semantic/Analyzer.h"
#include "Parser/Builder.h"
#include "Codegen/Generator.h"
#include "Lexer/Lexer.h"
#include "../Util/ParallelForEach.h"
#include "Parser/Parser.h"
#include <mutex>
#include <atomic>

void Wide::Compile(const Wide::Options::Clang& copts, const Wide::Options::LLVM& lopts, std::vector<std::string> files) {    
    Wide::Codegen::Generator Generator(lopts, copts);
    Wide::AST::Builder ASTBuilder;
    Wide::Semantic::Analyzer Sema(copts, &Generator);
    
    files.push_back("WideLibrary/stdlib.wide");

    std::mutex m;    
    std::atomic<bool> except = false;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = std::string(std::istream_iterator<char>(inputfile), std::istream_iterator<char>());
        Wide::Lexer::Invocation<decltype(contents.begin())> lex(largs, contents.begin(), contents.end());
        try {
            Wide::Parser::ParseModuleContents(lex, ASTBuilder, ASTBuilder.GetGlobalModule());
        } catch(std::runtime_error& e) {
            except = true;
            std::lock_guard<std::mutex> lock(m);
            std::cout << e.what() << "\n";
        } catch(...) {
            except = true;
            std::lock_guard<std::mutex> lock(m);
            std::cout << "Internal Compiler Error\n";
            __debugbreak();
        }
    });

    if (!except) {
        Sema(ASTBuilder.GetGlobalModule());
        Generator();
    }
}