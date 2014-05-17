#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/SemanticTest/test.h>

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
// Gotta include the header or creating JIT won't work... fucking LLVM.
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/DynamicLibrary.h>
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
void Jit(Wide::Options::Clang& copts, std::string file) {
#ifdef _MSC_VER
    const std::string MinGWInstallPath = "../Deployment/MinGW/";
    Wide::Driver::AddMinGWIncludePaths(copts, MinGWInstallPath);
#else
    Wide::Driver::AddLinuxIncludePaths(copts);
#endif

    auto AddStdlibLink = [&](llvm::ExecutionEngine* ee, llvm::Module* m) {
#ifdef _MSC_VER
        std::string err;
        auto libpath = MinGWInstallPath + "mingw32-dw2/bin/";
        for (auto lib : { "libgcc_s_dw2-1.dll", "libstdc++-6.dll" }) {
            if (llvm::sys::DynamicLibrary::LoadLibraryPermanently((libpath + lib).c_str(), &err))
                __debugbreak();
        }
#endif
        for (auto global_it = m->global_begin(); global_it != m->global_end(); ++global_it) {
            auto&& global = *global_it;
            auto name = global.getName().str();
            if (auto addr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(global.getName().str().c_str()))
                ee->addGlobalMapping(&global, addr);
        }
    };
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    Wide::Options::LLVM llvmopts;
    Wide::Codegen::Generator g(copts.TargetOptions.Triple);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
        auto m = a.GetGlobalModule()->AccessMember(a.GetGlobalModule()->BuildValueConstruction(Wide::Semantic::Expressions(), { a.GetGlobalModule(), root->where.front() }), "Main", { a.GetGlobalModule(), root->where.front() });
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->GetType()->Decay());
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve({}, a.GetGlobalModule()));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        name = f->GetName();
        f->ComputeBody();
        a.GenerateCode(g);
        g(llvmopts);
    }, { file });
    llvm::EngineBuilder b(g.module.get());
    auto mod = g.module.get();
    b.setAllocateGVsWithCode(false);
    b.setEngineKind(llvm::EngineKind::JIT);
    std::string errstring;
    b.setErrorStr(&errstring);
    auto ee = b.create();
    AddStdlibLink(ee, mod);
    ee->runStaticConstructorsDestructors(false);
    // Fuck you, shitty LLVM ownership semantics.
    if (ee)
        g.module.release();
    auto f = ee->FindFunctionNamed(name.c_str());
    auto result = ee->runFunction(f, std::vector<llvm::GenericValue>());
    ee->runStaticConstructorsDestructors(true);
    auto intval = result.IntVal.getLimitedValue();
    if (!intval)
        throw std::runtime_error("Test returned false.");
}
void Compile(const Wide::Options::Clang& copts, std::string file) {
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        if (Wide::Test::Test(a, nullptr, root, [&](Wide::Semantic::Error& r) {}))
            throw std::runtime_error("Test failed.");
    }, { file });
}

