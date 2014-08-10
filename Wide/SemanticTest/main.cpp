#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/DebugUtilities.h>
#include <boost/program_options.hpp>
#include <Wide/SemanticTest/test.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <Wide/Util/Concurrency/ConcurrentUnorderedSet.h>
#include <Wide/Util/Concurrency/ParallelForEach.h>

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
#endif
    // Enabling RTTI requires a Standard Library to be linked.
    // Else, there is an undefined reference to an ABI support class.
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
//    Jit(clangopts, "JITSuccess/Examples/ExportedFunctionsMemberSets.wide");
//    Compile(clangopts, "CompileFail/NoMember/ClangAccessPrivateMemberVariable.wide");
    std::unordered_map<std::string, std::function<bool()>> files;
#pragma warning(disable : 4800)
    for(auto mode : modes) {
        TestDirectory(mode.first, mode.first, argv[0], input.count("break"), files);
    }
    Wide::Concurrency::UnorderedSet<std::string> failed;
#ifdef _MSC_VER
    // LLVM's ExecuteAndWait can't handle parallel execution on non-Windows machines.
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), 
#else
    std::for_each(files.begin(), files.end(), 
#endif
        [&failed](std::pair<const std::string, std::function<bool()>>& ref) {
            if (ref.second())
                failed.insert(ref.first); 
        }
    );
    std::cout << "\n\nTotal succeeded: " << files.size() - failed.size() << " failed: " << failed.size() << "\n";
    if (failed.size() > 0)
        for(auto fail : failed)
            std::cout << "Failed: " << fail << "\n";
    if (input.count("break"))
        Wide::Util::DebugBreak();
    return failed.size() != 0;
}