// ClangExperiments.cpp : Defines the entry point for the console application.

#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Function.h>
#include <boost/program_options.hpp>
#include <memory>
#include <fstream>
#include <iostream>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_os_ostream.h>
#pragma warning(pop)

/*
 /usr/include/c++/4.7
 /usr/include/c++/4.7/x86_64-linux-gnu
 /usr/include/c++/4.7/backward
 /usr/lib/gcc/x86_64-linux-gnu/4.7/include
 /usr/local/include
 /usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed
 /usr/include/x86_64-linux-gnu
 /usr/include
*/
/*
Added include path: /usr/include/c++/4.7
Added include path: /usr/include/c++/4.7/x86_64-linux-gnu
Added include path: /usr/include/c++/4.7/backward
Added include path: /usr/lib/gcc/x86_64-linux-gnu/4.7/include
Added include path: /usr/local/include
Added include path: /usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed
Added include path: /usr/include/x86_64-linux-gnu
Added include path: /usr/include
*/
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2/x86_64-unknown-linux-gnu/"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2/backward"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/include"
// --include="usr/local/include"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/include-fixed"
// --include="usr/include/x86_64-linux-gnu"
// --include="usr/include"
// --include="/usr/include/c++/4.7" --include="/usr/include/c++/4.7/x86_64-linux-gnu" --include="/usr/include/c++/4.7/backward" --include="/usr/lib/gcc/x86_64-linux-gnu/4.7/include" --include="/usr/local/include" --include="/usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed" --include="/usr/include/x86_64-linux-gnu" --include="/usr/include" hello.wide

void SearchDirectory(std::string path, std::vector<std::string>& vec, std::string system) {
    auto end = llvm::sys::fs::directory_iterator();
    llvm::error_code fuck_error_codes;
    bool out = true;
    fuck_error_codes = llvm::sys::fs::is_directory(path, out);
    if (!out || fuck_error_codes) {
        std::cout << "Skipping " << path << " as a directory by this name did not exist.\n";
    }
    auto begin = llvm::sys::fs::directory_iterator(path, fuck_error_codes);
    std::set<std::string> entries;
    while (!fuck_error_codes && begin != end) {
        entries.insert(begin->path());
        begin.increment(fuck_error_codes);
    }
    if (llvm::sys::path::filename(path) == "System") {
        llvm::SmallVector<char, 1> fuck_out_parameters;
        llvm::sys::path::append(fuck_out_parameters, path, system);
        std::string systempath(fuck_out_parameters.begin(), fuck_out_parameters.end());
        SearchDirectory(systempath, vec, system);
        return;
    }
    for (auto file : entries) {
        bool isfile = false;
        llvm::sys::fs::is_regular_file(file, isfile);
        if (isfile) {
            if (llvm::sys::path::extension(file) == ".wide")
                vec.push_back(file);
        }
        llvm::sys::fs::is_directory(file, isfile);
        if (isfile) {
            SearchDirectory(file, vec, system);
        }
    }
}

int main(int argc, char** argv)
{
    boost::program_options::options_description desc;
    desc.add_options()
        ("help", "Print all available options")
#ifdef _MSC_VER
        ("mingw", boost::program_options::value<std::string>(), "The location of MinGW. Defaulted to \".\\MinGW\\\".")
#else
        ("gcc", boost::program_options::value<std::string>(), "The GCC version involved.")
#endif
        ("output", boost::program_options::value<std::string>(), "The output file. Defaulted to \"a.o\".")
        ("triple", boost::program_options::value<std::string>(), "The target triple. Defaulted to "
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
    ClangOpts.TargetOptions.Triple = input.count("triple") ? input["triple"].as<std::string>() : llvm::sys::getDefaultTargetTriple();
#endif

    ClangOpts.OnDiagnostic = [](std::string) {};

    ClangOpts.FrontendOptions.OutputFile = input.count("output") ? input["output"].as<std::string>() : "a.o";
    ClangOpts.LanguageOptions.CPlusPlus1y = true;
#ifdef _MSC_VER
    const std::string MinGWInstallPath = input.count("mingw") ? input["mingw"].as<std::string>() : ".\\MinGW\\";
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
#else
    if (input.count("gcc")) {
        auto gccver = input["gcc"].as<std::string>();
        auto base = "/usr/local/lib/gcc/x86_64-unknown-linux-gnu/" + gccver;
        ClangOpts.HeaderSearchOptions->AddPath(base + "/../../../../include/c++/" + gccver, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
        ClangOpts.HeaderSearchOptions->AddPath(base + "/../../../../include/c++/" + gccver + "/x86_64-unknown-linux-gnu/", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
        ClangOpts.HeaderSearchOptions->AddPath(base + "/../../../../include/c++/" + gccver + "/backward", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
        ClangOpts.HeaderSearchOptions->AddPath(base + "/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
        ClangOpts.HeaderSearchOptions->AddPath(base + "/include-fixed", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    }
    ClangOpts.HeaderSearchOptions->AddPath("/usr/local/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include/x86_64-linux-gnu", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
#endif
    //LLVMOpts.Passes.push_back(Wide::Options::CreateDeadCodeElimination());

    std::vector<std::string> files;
    if (input.count("input")) {
        files = input["input"].as<std::vector<std::string>>();
    } else {
        std::cout << "Didn't request any files to be compiled.\n";
        return 1;
    }

    if (input.count("include")) {
        for(auto path : input["include"].as<std::vector<std::string>>()) {
            //std::cout << "Added include path: " << path << "\n";
            ClangOpts.HeaderSearchOptions->AddPath(path, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
        }
    }

    //std::cout << "Triple: " << ClangOpts.TargetOptions.Triple << "\n";
    std::string stdlib = input.count("stdlib") ? input["stdlib"].as<std::string>() : "./WideLibrary/";

    auto trip = llvm::Triple(ClangOpts.TargetOptions.Triple);
    if (!trip.isOSWindows() && !trip.isOSLinux() && !trip.isMacOSX()) {
        std::cout << "Error: Wide only supports targetting Windows, Mac, and Linux right now.\n";
        return 1;
    }
    std::string name =
        trip.isOSWindows() ? "Windows" :
        trip.isOSLinux() ? "Linux" :
        "Mac";
    SearchDirectory(stdlib, files, name);

    try {
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
            Wide::Semantic::AnalyzeExportedFunctions(a);
            static const Wide::Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
            Wide::Semantic::Context c(a, location, [](Wide::Semantic::ConcreteExpression e) {}, a.GetGlobalModule());
            auto global = a.GetGlobalModule()->BuildValueConstruction({}, c);
            auto main = global.AccessMember("Main", c);
            if (!main)
                return;
            auto overset = dynamic_cast<Wide::Semantic::OverloadSet*>(main->t->Decay());
            auto f = overset->Resolve({}, a, a.GetGlobalModule());
            auto func = dynamic_cast<Wide::Semantic::Function*>(f);
            func->SetExportName("main");
            func->ComputeBody(a);
        }, Generator, files);
    } catch (Wide::Semantic::Error& e) {
        std::cout << "Error at " << to_string(e.location()) << "\n";
        std::cout << e.what() << "\n";
        return 1;
    }
    catch (std::exception& e) {
        std::cout << "Error:\n";
        std::cout << e.what() << "\n";
        return 1;
    }
    return 0;
}
