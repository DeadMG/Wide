// ClangExperiments.cpp : Defines the entry point for the console application.

#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <boost/program_options.hpp>
#include <memory>
#include <fstream>
#include <iostream>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/raw_os_ostream.h>
#pragma warning(pop)

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
        "i686-pc-mingw32.")
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
    Wide::LLVMCodegen::Generator Generator(LLVMOpts, ClangOpts.TargetOptions.Triple, [&](std::unique_ptr<llvm::Module> main) {
        llvm::PassManager pm;
        std::unique_ptr<llvm::TargetMachine> targetmachine;
        llvm::TargetOptions targetopts;
        std::string err;
        const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(ClangOpts.TargetOptions.Triple, err);
        targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(ClangOpts.TargetOptions.Triple, llvm::Triple(ClangOpts.TargetOptions.Triple).getArchName(), "", targetopts));
        std::ofstream file(ClangOpts.FrontendOptions.OutputFile, std::ios::trunc | std::ios::binary);
        llvm::raw_os_ostream out(file);
        llvm::formatted_raw_ostream format_out(out);
        targetmachine->addPassesToEmitFile(pm, format_out, llvm::TargetMachine::CodeGenFileType::CGFT_ObjectFile);
        pm.run(*main);
    });
    Wide::Driver::Compile(ClangOpts, [](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        static const Wide::Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
        auto std = a.GetGlobalModule()->AccessMember("Standard", a, location);
        if (!std)
            throw std::runtime_error("Fuck.");
        auto main = std->Resolve(nullptr).t->AccessMember("Main", a, location);
        if (!main)
            throw std::runtime_error("Fuck.");
        main->Resolve(nullptr).BuildCall(a, location);
    }, Generator, files);

    return 0;
}
