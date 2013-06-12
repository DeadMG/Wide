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
    
    files.push_back("WideLibrary/Standard/Algorithm/All.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Any.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Append.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Combiner.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Copy.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Count.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Drop.wide");
    files.push_back("WideLibrary/Standard/Algorithm/DropWhile.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Filter.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Find.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Flatten.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Fold.wide");
    files.push_back("WideLibrary/Standard/Algorithm/ForEach.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Map.wide");
    files.push_back("WideLibrary/Standard/Algorithm/None.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Take.wide");
    files.push_back("WideLibrary/Standard/Algorithm/TakeWhile.wide");
    files.push_back("WideLibrary/Standard/Containers/List.wide");
    files.push_back("WideLibrary/Standard/Containers/optional.wide");
    files.push_back("WideLibrary/Standard/Range/BackInserter.wide");
    files.push_back("WideLibrary/Standard/Range/Delimited.wide");
    files.push_back("WideLibrary/Standard/Range/Repeat.wide");
    files.push_back("WideLibrary/Standard/Range/StreamInserter.wide");
    files.push_back("WideLibrary/stdlib.wide");

    Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = std::string(std::istream_iterator<char>(inputfile), std::istream_iterator<char>());
        Wide::Lexer::Invocation<decltype(contents.begin())> lex(largs, contents.begin(), contents.end());
        try {
            Wide::Parser::ParseModuleContents(lex, ASTBuilder, ASTBuilder.GetGlobalModule());
        } catch(std::runtime_error& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Unknown Internal Compiler Error.\n");
        }
    });

    if (excepts.empty()) {
        Sema(ASTBuilder.GetGlobalModule());
        Generator();
    } else {
        for(auto&& msg : excepts) {
            std::cout << msg << "\n";
        }
    }
}

#include <llvm/Transforms/Scalar.h>

std::unique_ptr<llvm::Pass> Wide::Options::CreateDeadCodeElimination() {
    return std::unique_ptr<llvm::Pass>(llvm::createDeadCodeEliminationPass());
}