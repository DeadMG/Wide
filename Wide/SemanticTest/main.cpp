#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMGenerator.h>
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
    ;    
    boost::program_options::variables_map input;
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), input);
    } catch(std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }
    if (input.count("input")) {
        auto val = input["mode"].as<std::string>();
        if (val == "compile-success") {
            try {
                Compile(clangopts, input["input"].as<std::string>());
                return 0;
            } catch(...) {
                return 1;
            }
        }
        if (val == "compile-fail") {
            try {
                Compile(clangopts, input["input"].as<std::string>());
                return 1;
            } catch(...) {
                return 0;
            }
        }
        //atexit([]{ __debugbreak(); });
        if (val == "jit-success") {
            try {
                Jit(clangopts, input["input"].as<std::string>());
                return 0;
            } catch(...) {
                return 1;
            }
        }
        if (val == "jit-fail") {
            try {
                Jit(clangopts, input["input"].as<std::string>());
                return 1;
            } catch(...) {
                return 0;
            }
        }
        __debugbreak();
    }
    bool failure = false;
    auto run_test_process = [&](std::string file, std::string mode) {
        auto modearg = "--mode=" + mode;
        std::string arguments = "--input=" + file;
        const char* args[] = { argv[0], arguments.c_str(), modearg.c_str(), nullptr };
        std::string err = "";
        bool failed = false;
        auto ret = llvm::sys::ExecuteAndWait(
            argv[0],
            args,
            nullptr,
            nullptr,
            0,
            0,
            &err,
            &failed
        );
        if (failed)
            __debugbreak();
        if (ret) {
            failure = true;
            std::cout << mode << " failed: " << file << "\n";
        } else
            std::cout << mode << " succeeded: " << file << "\n";
    };

    // Run with Image File Options attaching a debugger to debug a test.
    // Run without to see test results.

    std::string compile_success[] = { 
        "IntegerOperations.wide", 
        "DeferredVariable.wide",
        "DeferredExpressionStatement.wide",
        "BooleanOperations.wide"
    };
    for(auto file : compile_success) {
        run_test_process(file, "compile-success");
    }
    
    std::string jit_success[] = {
        "BooleanShortCircuit.wide",
        "ClangADL.wide",
        "ClangMixedADL.wide",
        "ClangMemberFunction.wide",
        "ClangMemberOperator.wide",
        "RecursiveTypeInference.wide",
        "CorecursiveTypeInference.wide",
        "MemberCall.wide",
        "MemberInitialization.wide",
        "AcceptQualifiedThis.wide",
        "DeferredLambda.wide",
        "DecltypeNoDestruction.wide",
        "PrimitiveADL.wide", 
        "SimpleRAII.wide",
        "IfConditionScope.wide"
    };
    for(auto file : jit_success) {
        run_test_process(file, "jit-success");
    }
    
    std::string compile_fail[] = {
        "RejectNontypeFunctionArgumentExpression.wide",
        "SubmoduleNoQualifiedLookup.wide",
        "RejectLvalueQualifiedThis.wide",
        "RejectRvalueQualifiedThis.wide"
    };
    for(auto file : compile_fail)
        run_test_process(file, "compile-fail");
    
    //atexit([] { __debugbreak(); });
    //std::set_terminate([] { __debugbreak(); });
    //Jit(clangopts, "IfConditionScope.wide");

    __debugbreak();
    return failure;
}