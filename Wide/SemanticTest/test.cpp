#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/SemanticTest/test.h>

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#pragma warning(pop)

results TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak) {
    unsigned tests_failed = 0;
    unsigned tests_succeeded = 0;
    auto run_test_process = [&](std::string file) {
        auto modearg = "--mode=" + mode;
        std::string arguments = "--input=" + file;
        const char* args[] = { program.c_str(), arguments.c_str(), modearg.c_str(), nullptr };
        std::string err = "";
        bool failed = false;
        auto timeout = debugbreak ? 0 : 1;
        auto ret = llvm::sys::ExecuteAndWait(
            args[0],
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

    auto end = llvm::sys::fs::directory_iterator();
    llvm::error_code fuck_error_codes;
    bool out = true;
    fuck_error_codes = llvm::sys::fs::is_directory(path, out);
    if (!out || fuck_error_codes) {
        std::cout << "Skipping " << path << " as a directory by this name did not exist.\n";
        results r = { 0, 0 };
        return r;
    }
    auto begin = llvm::sys::fs::directory_iterator(path, fuck_error_codes);
    std::set<std::string> entries;
    while (!fuck_error_codes && begin != end) {
        entries.insert(begin->path());
        begin.increment(fuck_error_codes);
    }
    for (auto file : entries) {
        bool isfile = false;
        llvm::sys::fs::is_regular_file(file, isfile);
        if (isfile) {
            if (llvm::sys::path::extension(file) == ".wide")
                run_test_process(file);
        }
        llvm::sys::fs::is_directory(file, isfile);
        if (isfile) {
            auto result = TestDirectory(file, mode, program, debugbreak);
            tests_succeeded += result.passes;
            tests_failed += result.fails;
        }
    }
    std::cout << path << " succeeded: " << tests_succeeded << " failed: " << tests_failed << "\n";
    results r = { tests_succeeded, tests_failed };
    return r;
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
        auto mod = main.get();
        if (ee)
            main.release();
        auto f = ee->FindFunctionNamed(name.c_str());
        auto result = ee->runFunction(f, std::vector<llvm::GenericValue>());
        auto intval = result.IntVal.getLimitedValue();
        if (!intval)
            throw std::runtime_error("Test returned false.");
    });
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Semantic::Context c(a, loc, [](Wide::Semantic::ConcreteExpression e) {}, a.GetGlobalModule());
        auto m = a.GetGlobalModule()->AccessMember(a.GetGlobalModule()->BuildValueConstruction({}, c), "Main", c);
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->t);
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve(std::vector<Wide::Semantic::Type*>(), a));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        name = f->GetName();
        f->BuildCall(f->BuildValueConstruction({}, c), {}, c);
    }, g, { file });
}
void Compile(const Wide::Options::Clang& copts, std::string file) {
    Wide::Driver::NullGenerator mockgen(copts.TargetOptions.Triple);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Test::Test(a, nullptr, root, [&](Wide::Lexer::Range r, Wide::Semantic::Error e) {
            throw Wide::Semantic::SemanticError(r, e);
        }, mockgen, false);
    }, mockgen, { file });
}

