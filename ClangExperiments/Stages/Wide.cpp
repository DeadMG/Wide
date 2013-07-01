#include "Wide.h"
#include "Semantic/Analyzer.h"
#include "Parser/Builder.h"
#include "Codegen/Generator.h"
#include "Lexer/Lexer.h"
#include "../Util/ParallelForEach.h"
#include "Parser/Parser.h"
#include "../Util/Ranges/IStreamRange.h"
#include <mutex>
#include <atomic>
#include <sstream>

void Wide::Compile(const Wide::Options::Clang& copts, const Wide::Options::LLVM& lopts, std::vector<std::string> files) {    
    Wide::Codegen::Generator Generator(lopts, copts);
    Wide::AST::Builder ASTBuilder;
    Wide::Semantic::Analyzer Sema(copts, &Generator);
    
    files.push_back("WideLibrary/Standard/Algorithm/All.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Any.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Append.wide");
    files.push_back("WideLibrary/Standard/Algorithm/Combiner.wide");
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
    files.push_back("WideLibrary/Standard/Containers/optional.wide");
    files.push_back("WideLibrary/Standard/IO/Stream.wide");
    files.push_back("WideLibrary/Standard/Range/BackInserter.wide");
    files.push_back("WideLibrary/Standard/Range/Delimited.wide");
    files.push_back("WideLibrary/Standard/Range/Repeat.wide");
    files.push_back("WideLibrary/Standard/Range/StreamInserter.wide");
    files.push_back("WideLibrary/Standard/Utility/Move.wide");
    files.push_back("WideLibrary/stdlib.wide");

    Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = Wide::Range::IStreamRange(inputfile);
        Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents);
        try {
            Wide::Parser::ParseGlobalModuleContents(lex, AST::ThreadLocalBuilder(ASTBuilder), ASTBuilder.GetGlobalModule());
        } catch(Wide::Parser::UnrecoverableError& e) {
            std::stringstream str;
            str << "Error in file " << filename << ", line " << e.where().begin.line << " column " << e.where().begin.column << ":\n";
            str << e.what() << "\n";
            excepts.push_back(str.str());
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