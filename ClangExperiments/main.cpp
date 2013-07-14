// ClangExperiments.cpp : Defines the entry point for the console application.

#include <Semantic/ClangOptions.h>
#include <Codegen/LLVMOptions.h>
#include <Semantic/Analyzer.h>
#include <Parser/Builder.h>
#include <Codegen/Generator.h>
#include <Lexer/Lexer.h>
#include <Util/ParallelForEach.h>
#include <Parser/Parser.h>
#include <boost/program_options.hpp>
#include <Util/Ranges/IStreamRange.h>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iostream>

#ifndef _MSC_VER
#include <llvm/Support/Host.h>
#endif

void Compile(const Wide::Options::Clang& copts, const Wide::Options::LLVM& lopts, const std::vector<std::string>& files) {    
    Wide::Codegen::Generator Generator(lopts, copts.FrontendOptions.OutputFile, copts.TargetOptions.Triple);
    Wide::AST::Builder ASTBuilder;
    Wide::Semantic::Analyzer Sema(copts, &Generator);
    
    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = Wide::Range::IStreamRange(inputfile);
        Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents);
        auto parsererrorhandler = [&](Wide::Lexer::Range where, Wide::Parser::Error what) {
            std::stringstream str;
            str << "Error in file " << filename << ", line " << where.begin.line << " column " << where.begin.column << ":\n";
            str << Wide::Parser::ErrorStrings.at(what);
            excepts.push_back(str.str());
        };
        try {
            Wide::Parser::ParseGlobalModuleContents(lex, Wide::AST::ThreadLocalBuilder(ASTBuilder, parsererrorhandler), ASTBuilder.GetGlobalModule());
        } catch(Wide::Parser::UnrecoverableError& e) {
            parsererrorhandler(e.where(), e.error());
        } catch(std::exception& e) {
            excepts.push_back(e.what());
        } catch(...) {
            excepts.push_back("Internal Compiler Error");
        }
    });

    if (excepts.empty()) {
        try {
           Sema(ASTBuilder.GetGlobalModule());
           Generator();
        } catch(std::exception& e) {
            std::cout << e.what() << "\n";
            //std::cin.get();
        }
    } else {
        for(auto&& msg : excepts) {
            std::cout << msg << "\n";
        }
        //std::cin.get();
    }
}

int main(int argc, char** argv)
{
    boost::program_options::options_description desc;
    desc.add_options()
        ("help", "Print all available options")
#ifdef _MSC_VER
        ("mingw", boost::program_options::value<std::string>(), "The location of MinGW. Defaulted to \".\\MinGW\\\".")
#endif
        ("output", boost::program_options::value<std::string>(), "The output file. Defaulted to \"a.o\".")
        ("triple", boost::program_options::value<std::string>(), "The target triple. Defaulted to"
#ifdef _MSC_VER
        "\"i686-pc-mingw32\".")
#else
        "the LLVM Host triple.")
#endif
        ("input", boost::program_options::value<std::vector<std::string>>(), "One input file. May be specified multiple times.")
        ("stdlib", boost::program_options::value<std::string>(), "The Standard library path. Defaulted to \".\\WideLibrary\\\".")
        ("include", boost::program_options::value<std::vector<std::string>>(), "One include path. May be specified multiple times.")
    ;

    boost::program_options::positional_options_description positional;
    positional.add("input", -1);
    
    boost::program_options::variables_map input;
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).positional(positional).run(), input);
    } catch(std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }

    if(input.count("help")) {
        std::cout << desc;
        return 0;
    }

    Wide::Options::Clang ClangOpts;
    Wide::Options::LLVM LLVMOpts;

#ifdef _MSC_VER
    ClangOpts.TargetOptions.Triple = input.count("triple") ? input["triple"].as<std::string>() : "i686-pc-mingw32";
#else
    ClangOpts.TargetOptions.Triple = input.count("triple") ? input["triple"].as<std::string>() : llvm::sys::GetDefaultTargetTriple();
#endif

    ClangOpts.FrontendOptions.OutputFile = input.count("output") ? input["output"].as<std::string>() : "a.o";
    ClangOpts.LanguageOptions.CPlusPlus1y = true;
    
#ifdef _MSC_VER
    const std::string MinGWInstallPath = input.count("mingw") ? input["mingw"].as<std::string>() : ".\\MinGW\\";
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
#endif

    //LLVMOpts.Passes.push_back(Wide::Options::CreateDeadCodeElimination());

    std::vector<std::string> files;
    if (input.count("input"))
        files = input["input"].as<std::vector<std::string>>();
    else {
        std::cout << "Didn't request any files to be compiled.\n";
        return 1;
    }

    std::string stdlib = input.count("stdlib") ? input["stdlib"].as<std::string>() : ".\\WideLibrary\\";

    files.push_back(stdlib + "Standard/Algorithm/All.wide");
    files.push_back(stdlib + "Standard/Algorithm/Any.wide");
    files.push_back(stdlib + "Standard/Algorithm/Append.wide");
    files.push_back(stdlib + "Standard/Algorithm/Combiner.wide");
    files.push_back(stdlib + "Standard/Algorithm/Count.wide");
    files.push_back(stdlib + "Standard/Algorithm/Drop.wide");
    files.push_back(stdlib + "Standard/Algorithm/DropWhile.wide");
    files.push_back(stdlib + "Standard/Algorithm/Filter.wide");
    files.push_back(stdlib + "Standard/Algorithm/Find.wide");
    files.push_back(stdlib + "Standard/Algorithm/Flatten.wide");
    files.push_back(stdlib + "Standard/Algorithm/Fold.wide");
    files.push_back(stdlib + "Standard/Algorithm/ForEach.wide");
    files.push_back(stdlib + "Standard/Algorithm/Map.wide");
    files.push_back(stdlib + "Standard/Algorithm/None.wide");
    files.push_back(stdlib + "Standard/Algorithm/Take.wide");
    files.push_back(stdlib + "Standard/Algorithm/TakeWhile.wide");
    files.push_back(stdlib + "Standard/Containers/optional.wide");
    files.push_back(stdlib + "Standard/IO/Stream.wide");
    files.push_back(stdlib + "Standard/Range/BackInserter.wide");
    files.push_back(stdlib + "Standard/Range/Delimited.wide");
    files.push_back(stdlib + "Standard/Range/Repeat.wide");
    files.push_back(stdlib + "Standard/Range/StreamInserter.wide");
    files.push_back(stdlib + "Standard/Utility/Move.wide");
    files.push_back(stdlib + "stdlib.wide");
    Compile(ClangOpts, LLVMOpts, files);

	return 0;
}
