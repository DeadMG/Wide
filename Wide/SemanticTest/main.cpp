#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/DebugUtilities.h>
#include <boost/program_options.hpp>
#include <Wide/SemanticTest/test.h>
#include <unordered_map>
#include <string>
#include <functional>

#pragma warning(push, 0)
#include <llvm/Support/Host.h>
#include <llvm/ADT/Triple.h>
#pragma warning(pop)

int main(int argc, char** argv) {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = llvm::sys::getProcessTriple();
#ifdef _MSC_VER
    // MCJIT can't handle non-ELF on Windows for some reason.
    clangopts.TargetOptions.Triple += "-elf";
#else
    // getProcessTriple returns x86_64 but TargetMachine expects x86-64.
    // Fixing it only makes the problem WORSE.
    //if (llvm::Triple(clangopts.TargetOptions.Triple).getArch() == llvm::Triple::ArchType::x86_64)
    //    clangopts.TargetOptions.Triple[3] = '-';
#endif
    // Enabling RTTI requires a Standard Library to be linked.
    // Else, there is an undefined reference to an ABI support class.
    //clangopts.LanguageOptions.RTTI = false;
    boost::program_options::options_description desc;
    desc.add_options()
        ("input", boost::program_options::value<std::string>(), "One input file. May be specified multiple times.")
        ("mode", boost::program_options::value<std::string>(), "The testing mode.")
        ("break", "Break when the tests are succeeded to view the output.")
        ;
    boost::program_options::variables_map input;
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), input);
    } catch (std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }
    std::unordered_map<std::string, std::function<int()>> modes([&]() -> std::unordered_map<std::string, std::function<int()>> {
        std::unordered_map<std::string, std::function<int()>> ret;

        ret["CompileFail"] = [&] {
            try {
                Compile(clangopts, input["input"].as<std::string>());
                return 0;
            } catch (std::runtime_error& e) {
                std::cout << e.what() << "\n";
                return 1;
            }
        };
        ret["JITSuccess"] = [&] {
            try {
                Jit(clangopts, input["input"].as<std::string>());
                return 0;
            } catch (...) {
                return 1;
            }
        };
        return ret;
    }());
    if (input.count("input")) {
        if (modes.find(input["mode"].as<std::string>()) != modes.end())
            return modes[input["mode"].as<std::string>()]();
        return 1;
    }
    unsigned total_failed = 0;
    unsigned total_succeeded = 0;

    // Run with Image File Options attaching a debugger to debug a test.
    // Run without to see test results.
#pragma warning(disable : 4800)
    for(auto mode : modes) {
        auto result = TestDirectory(mode.first, mode.first, argv[0], input.count("break"));
        total_succeeded += result.passes;
        total_failed += result.fails;
    }
    std::cout << "Total succeeded: " << total_succeeded << " failed: " << total_failed;
    //Jit(clangopts, "JITSuccess/CPPInterop/ExportRefQualifier.wide");
    //Compile(clangopts, "CompileFail/OverloadResolutionFailure/CopyWithMoveOnlyElement.wide");
    if (input.count("break"))
        Wide::Util::DebugBreak();
    return total_failed != 0;
}
