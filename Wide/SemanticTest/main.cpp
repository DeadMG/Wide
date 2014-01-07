#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Util/DebugUtilities.h>
#include <boost/program_options.hpp>

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#pragma warning(pop)

void Compile(const Wide::Options::Clang& copts, std::string file) {
    Wide::Driver::NullGenerator mockgen(copts.TargetOptions.Triple);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Test::Test(a, nullptr, root, [&](Wide::Lexer::Range r, Wide::Semantic::Error e) { 
            throw Wide::Semantic::SemanticError(r, e); 
        }, mockgen, false);
    }, mockgen, { file });
}

void Compile(const Wide::Options::Clang& copts, std::string file, Wide::Semantic::Error expected, int linebegin, int columnbegin, int lineend, int columnend) {
    Wide::Driver::NullGenerator mockgen(copts.TargetOptions.Triple);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Test::Test(a, nullptr, root, [&](Wide::Lexer::Range r, Wide::Semantic::Error e) {
            // Would be nice to use || here but MSVC no compile it.
            if ((*r.begin.name) != file)
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if ((*r.end.name) != file)                
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if (r.begin.line != linebegin)
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if (r.begin.column != columnbegin)                
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if (r.end.line != lineend)
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if (r.end.column != columnend)
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
            if (expected != e)
                throw std::runtime_error("Error ocurred, but it did not match the expected.");
        }, mockgen, false);
    }, mockgen, { file });
}

void Jit(const Wide::Options::Clang& copts, std::string file) {
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    Wide::Options::LLVM llvmopts;
    Wide::LLVMCodegen::Generator g(llvmopts, copts.TargetOptions.Triple, [&](std::unique_ptr<llvm::Module> main) {
        llvm::EngineBuilder b(main.get());
        b.setAllocateGVsWithCode(false);
        b.setEngineKind(llvm::EngineKind::JIT);
        std::string errstring;
        b.setErrorStr(&errstring);
        auto ee = b.create();
        // Fuck you, shitty LLVM ownership semantics.
        if (ee)
            main.release();
        auto f = ee->FindFunctionNamed(name.c_str());
        auto result = ee->runFunction(f, std::vector<llvm::GenericValue>());
        auto intval = result.IntVal.getLimitedValue();
        if (!intval)
            throw std::runtime_error("Test returned false.");
    });
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Semantic::Context c(a, loc, [](Wide::Semantic::ConcreteExpression e) {});
        auto m = a.GetGlobalModule()->AccessMember(a.GetGlobalModule()->BuildValueConstruction(c), "Main", c);
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->Resolve(nullptr).t);
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve(std::vector<Wide::Semantic::Type*>(), a));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        name = f->GetName();
        f->BuildCall(f->BuildValueConstruction(c), std::vector<Wide::Semantic::ConcreteExpression>(), c);
    }, g, { file });
}

int main(int argc, char** argv) {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
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
        ret["CompileSuccess"] = [&] {
            try {
                Compile(clangopts, input["input"].as<std::string>());
                return 0;
            } catch (...) {
                return 1;
            }
        };
        ret["CompileFail"] = [&] {
            try {
                Compile(clangopts, input["input"].as<std::string>());
                return 1;
            } catch (...) {
                return 0;
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
        ret["JITFail"] = [&] {
            try {
                Jit(clangopts, input["input"].as<std::string>());
                return 1;
            } catch (...) {
                return 0;
            }
        };
        return ret;
    }());
    if (input.count("input")) {
        if (modes.find(input["mode"].as<std::string>()) != modes.end())
            return modes[input["mode"].as<std::string>()]();
        return 1;
    }
    unsigned tests_failed = 0;
    unsigned tests_succeeded = 0;
    unsigned total_failed = 0;
    unsigned total_succeeded = 0;
    auto run_test_process = [&](std::string file, std::string mode) {
        auto modearg = "--mode=" + mode;
        std::string arguments = "--input=" + file;
        const char* args[] = { argv[0], arguments.c_str(), modearg.c_str(), nullptr };
        std::string err = "";
        bool failed = false;
        auto timeout = input.count("break") ? 0 : 1;
#ifdef _MSC_VER
        auto ret = llvm::sys::ExecuteAndWait(
            argv[0],
#else
        auto ret = llvm::sys::Program::ExecuteAndWait(
            llvm::sys::Path(argv[0]),
#endif
            args,
            nullptr,
            nullptr,
            timeout,
            0,
            &err,
            &failed
            );

        if (failed || ret) {
            tests_failed++;
            std::cout << mode << " failed: " << file << "\n";
        } else {
            tests_succeeded++;
            std::cout << mode << " succeeded: " << file << "\n";
        }
    };

    // Run with Image File Options attaching a debugger to debug a test.
    // Run without to see test results.
    for (auto mode : modes) {
        auto end = llvm::sys::fs::directory_iterator();
        llvm::error_code fuck_error_codes;
        bool out = true;
        fuck_error_codes = llvm::sys::fs::is_directory(mode.first, out);
        if (!out || fuck_error_codes) {
            std::cout << "Skipping " << mode.first << " as a directory by this name did not exist.\n";
            continue;
        }
        auto begin = llvm::sys::fs::directory_iterator(mode.first, fuck_error_codes);
        std::set<std::string> entries;
        while (!fuck_error_codes && begin != end) {
            entries.insert(begin->path());
            begin.increment(fuck_error_codes);
        }
        for (auto file : entries) {
            bool isfile = false;
            llvm::sys::fs::is_regular_file(file, isfile);
            if (isfile)
                if (llvm::sys::path::extension(file) == ".wide")
                    run_test_process(file, mode.first);
        }
        std::cout << mode.first << " succeeded: " << tests_succeeded << " failed: " << tests_failed << "\n";
        total_succeeded += tests_succeeded;
        total_failed += tests_failed;
        tests_succeeded = 0;
        tests_failed = 0;
    }
    std::cout << "Total succeeded: " << total_succeeded << " failed: " << total_failed;
    //atexit([] { __debugbreak(); });
    //std::set_terminate([] { __debugbreak(); });
    //Jit(clangopts, "JITSuccess/CorecursiveTypeInference.wide");
    if (input.count("break"))
        Wide::Util::DebugBreak();
    return tests_failed != 0;
}
