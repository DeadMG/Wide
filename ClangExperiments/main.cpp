// ClangExperiments.cpp : Defines the entry point for the console application.

#include <Semantic/ClangOptions.h>
#include <Codegen/LLVMOptions.h>
#include <Semantic/Analyzer.h>
#include <Parser/Builder.h>
#include <Codegen/Generator.h>
#include <Lexer/Lexer.h>
#include <Util/ParallelForEach.h>
#include <Parser/Parser.h>
#include <Util/Ranges/IStreamRange.h>
#include <mutex>
#include <atomic>
#include <sstream>

void Compile(const Wide::Options::Clang& copts, const Wide::Options::LLVM& lopts, std::vector<std::string> files) {    
    Wide::Codegen::Generator Generator(lopts, copts.FrontendOptions.OutputFile, copts.TargetOptions.Triple);
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

    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = Wide::Range::IStreamRange(inputfile);
        Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents);
        try {
            Wide::Parser::ParseGlobalModuleContents(lex, Wide::AST::ThreadLocalBuilder(ASTBuilder), ASTBuilder.GetGlobalModule());
        } catch(Wide::Parser::UnrecoverableError& e) {
            std::stringstream str;
            str << "Error in file " << filename << ", line " << e.where().begin.line << " column " << e.where().begin.column << ":\n";
            str << e.what();
            excepts.push_back(str.str());
        } catch(std::exception& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Internal Compiler Error");
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

int main()
{
    Wide::Options::Clang ClangOpts;
    Wide::Options::LLVM LLVMOpts;

    ClangOpts.TargetOptions.Triple = "i686-pc-mingw32";
    ClangOpts.FrontendOptions.OutputFile = "yay.o";
    ClangOpts.LanguageOptions.CPlusPlus1y = true;
    
    const std::string MinGWInstallPath = "D:\\Backups\\unsorted\\MinGW\\mingw32-dw2\\";

    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);

    //LLVMOpts.Passes.push_back(Wide::Options::CreateDeadCodeElimination());

    std::vector<std::string> files;
    files.push_back("main.wide");
    Compile(ClangOpts, LLVMOpts, files);
    
	return 0;
}
