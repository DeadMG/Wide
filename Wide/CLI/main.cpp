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

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

std::unordered_set<std::string> SearchDirectory(std::string path, std::string system) {
    std::unordered_set<std::string> ret;
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
        return SearchDirectory(systempath, system);
    }
    for (auto file : entries) {
        bool isfile = false;
        llvm::sys::fs::is_regular_file(file, isfile);
        if (isfile) {
            if (llvm::sys::path::extension(file) == ".wide")
                ret.insert(file);
        }
        llvm::sys::fs::is_directory(file, isfile);
        if (isfile) {
            auto more = SearchDirectory(file, system);
            ret.insert(more.begin(), more.end());
        }
    }
    return ret;
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
        ("version", "The build of Wide.")
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
        std::cout << "Local development build.\n";
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
    //LLVMOpts.Passes.push_back(Wide::Options::CreateDeadCodeElimination());

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

    //std::cout << "Triple: " << ClangOpts.TargetOptions.Triple << "\n";
    std::string stdlib = input.count("stdlib") ? input["stdlib"].as<std::string>() : default_dir;

    auto trip = llvm::Triple(ClangOpts.TargetOptions.Triple);
    if (!trip.isOSWindows() && !trip.isOSLinux() && !trip.isMacOSX()) {
        std::cout << "Error: Wide only supports targetting Windows, Mac, and Linux right now.\n";
        return 1;
    }
    std::string name =
        trip.isOSWindows() ? "Windows" :
        trip.isOSLinux() ? "Linux" :
        "Mac";
    auto stdfiles = SearchDirectory(stdlib, name);
    auto final_files = files;
    final_files.insert(stdfiles.begin(), stdfiles.end());
    ClangOpts.HeaderSearchOptions->AddPath(stdlib, clang::frontend::IncludeDirGroup::System, false, false);

    try {
        llvm::LLVMContext con;
        auto mod = Wide::Util::CreateModuleForTriple(ClangOpts.TargetOptions.Triple, con);
        Wide::Driver::Compile(ClangOpts, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
            Wide::Semantic::AnalyzeExportedFunctions(a);
            static const Wide::Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
            Wide::Semantic::Context c(a.GetGlobalModule(), location);
            auto global = a.GetGlobalModule()->BuildValueConstruction({}, c);
            auto main = global->GetType()->AccessMember(std::move(global), "Main", c);
            if (!main)
                return;
            auto overset = dynamic_cast<Wide::Semantic::OverloadSet*>(main->GetType()->Decay());
            auto f = overset->Resolve({}, c.from);
            auto func = dynamic_cast<Wide::Semantic::Function*>(f);
            func->AddExportName(std::string("main"));
            func->ComputeBody();
            a.GenerateCode(mod.get());
            if (llvm::verifyModule(*mod, llvm::VerifierFailureAction::PrintMessageAction))
                throw std::runtime_error("Internal compiler error: An LLVM module failed verification.");
            Wide::Driver::PrintUnusedFunctionsWarning(files, a);
            Wide::Driver::PrintNonvoidFalloffWarning(files, a);
        }, std::vector<std::string>(final_files.begin(), final_files.end()));
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
        pm.run(*mod);
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
