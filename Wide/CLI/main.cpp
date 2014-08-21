// ClangExperiments.cpp : Defines the entry point for the console application.

#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Util/Driver/IncludePaths.h>
#include <boost/program_options.hpp>
#include <memory>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Util/Driver/StdlibDirectorySearch.h>
#include <Wide/CLI/Link.h>
#include <Wide/CLI/Export.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

int main(int argc, char** argv)
{
    std::unordered_map<std::string, std::pair<std::function<void(boost::program_options::options_description&)>, std::function<void(llvm::LLVMContext&, llvm::Module*, std::vector<std::string>, const Wide::Options::Clang&, const boost::program_options::variables_map&)>>> actions = {
        {
            "link", 
            {
                [](boost::program_options::options_description& desc) {
                    Wide::Driver::AddLinkOptions(desc);
                },
                [](llvm::LLVMContext& con, llvm::Module* mod, std::vector<std::string> files, const Wide::Options::Clang& opts, const boost::program_options::variables_map& args) {
                    Wide::Driver::Link(con, mod, files, opts, args);
                }
            }
        },
        {
            "export",
            {
                [](boost::program_options::options_description& desc) {
                    Wide::Driver::AddExportOptions(desc);
                },
                [](llvm::LLVMContext& con, llvm::Module* mod, std::vector<std::string> files, const Wide::Options::Clang& opts, const boost::program_options::variables_map& args) {
                    Wide::Driver::Export(con, mod, files, opts, args);
                }
            }
        }
    };
    boost::program_options::options_description desc;
    std::string modes = "Sets the compiler mode. The default is link. The possible modes are: ";
    for (auto&& pair : actions) {
        pair.second.first(desc);
        modes += pair.first;
        if (&pair != &*(--actions.end()))
            modes += ", ";
    }
    desc.add_options()
        ("help", "Print all available options")
#ifdef _MSC_VER
        ("mingw", boost::program_options::value<std::string>(), "The location of MinGW. Defaulted to \".\\MinGW\\\".")
#else
        ("gcc", boost::program_options::value<std::string>(), "The GCC version involved.")
#endif
        ("triple", boost::program_options::value<std::string>(), "The target triple. Defaulted to "
#ifdef _MSC_VER
        "i686-pc-mingw32.")
#else
        "the LLVM Host triple.")
#endif
        ("input", boost::program_options::value<std::vector<std::string>>(), "One input file. May be specified multiple times.")
        ("stdlib", boost::program_options::value<std::string>(), "The Standard library path. Defaulted to \".\\WideLibrary\\\".")
        ("include", boost::program_options::value<std::vector<std::string>>(), "One include path. May be specified multiple times.")
        ("mode", boost::program_options::value<std::string>(), modes.c_str())
        ("version", "Outputs the build of Wide.")
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

    if (input.count("version")) {
#ifdef TEAMCITY
        std::cout << "TeamCity build " << TEAMCITY << "\n";
#else
        std::cout << "Local development build, date " __DATE__ " time " __TIMESTAMP__ ".\n";
#endif
        return 0;
    }

    if(input.count("help")) {
        std::cout << desc;
        return 0;
    }

    Wide::Options::Clang ClangOpts;

#ifdef _MSC_VER
    ClangOpts.TargetOptions.Triple = input.count("triple") ? input["triple"].as<std::string>() : "i686-pc-mingw32";
#else
    ClangOpts.TargetOptions.Triple = input.count("triple") ? input["triple"].as<std::string>() : llvm::sys::getDefaultTargetTriple();
#endif

    ClangOpts.OnDiagnostic = [](std::string) {};

    ClangOpts.FrontendOptions.OutputFile = input.count("output") ? input["output"].as<std::string>() : "a.o";
    ClangOpts.LanguageOptions.CPlusPlus1y = true;
#ifdef _MSC_VER
    Wide::Driver::AddMinGWIncludePaths(ClangOpts, input.count("mingw") ? input["mingw"].as<std::string>() : ".\\MinGW\\");
#else
    if (input.count("gcc"))
        Wide::Driver::AddLinuxIncludePaths(ClangOpts,  input["gcc"].as<std::string>());
    else {
        try {
            Wide::Driver::AddLinuxIncludePaths(ClangOpts);
        } catch(std::exception& e) {
            std::cout << e.what();
            return 1;
        }
    }
#endif

    std::unordered_set<std::string> files;
    if (input.count("input")) {
        auto list = input["input"].as<std::vector<std::string>>();
        files.insert(list.begin(), list.end());
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

    std::string default_dir = "./WideLibrary/";
    auto selfpath = llvm::sys::fs::getMainExecutable(argv[0], (void*)&main);
    if (selfpath != "") { // Fucking bullshit error handling
        llvm::SmallVector<char, 5> fuck_small_vector(selfpath.begin(), selfpath.end());
        llvm::sys::path::remove_filename(fuck_small_vector);
        llvm::sys::path::append(fuck_small_vector, "WideLibrary");
        std::string result_path(fuck_small_vector.begin(), fuck_small_vector.end());
        if (llvm::sys::fs::is_directory(result_path)) {
            default_dir = result_path;
        }
    }

    std::string stdlib = input.count("stdlib") ? input["stdlib"].as<std::string>() : default_dir;

    auto stdfiles = Wide::Driver::SearchStdlibDirectory(stdlib, ClangOpts.TargetOptions.Triple);
    auto final_files = files;
    final_files.insert(stdfiles.begin(), stdfiles.end());
    ClangOpts.HeaderSearchOptions->AddPath(stdlib, clang::frontend::IncludeDirGroup::System, false, false);

    llvm::LLVMContext con;
    auto mod = Wide::Util::CreateModuleForTriple(ClangOpts.TargetOptions.Triple, con);
    auto mode = input.count("mode") ? input["mode"].as<std::string>() : "link";
    if (actions.find(mode) == actions.end()) {
        std::cout << "Error: Unrecognized mode " << mode << "\n";
        return 1;
    }
    
    try {
        actions.at(mode).second(con, mod.get(), std::vector<std::string>(final_files.begin(), final_files.end()), ClangOpts, input);
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
