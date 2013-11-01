#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMGenerator.h>

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Test/Catch.h>

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#pragma warning(pop)

void Compile(const Wide::Options::Clang& copts, std::initializer_list<std::string> files) {
    Wide::Driver::NullGenerator mockgen(copts.TargetOptions.Triple);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {
        Wide::Test::Test(a, nullptr, root, [&](Wide::Lexer::Range r, Wide::Semantic::Error e) { throw Wide::Semantic::SemanticError(r, e); }, mockgen, false);
    }, mockgen, files);
}

void Interpret(const Wide::Options::Clang& copts, std::initializer_list<std::string> files) {
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    Wide::Options::LLVM llvmopts;
    Wide::LLVMCodegen::Generator g(llvmopts, copts.TargetOptions.Triple, [&](std::unique_ptr<llvm::Module> main) {
        llvm::EngineBuilder b(main.get());
        b.setAllocateGVsWithCode(false);
        b.setEngineKind(llvm::EngineKind::Interpreter);
        std::string errstring;
        b.setErrorStr(&errstring);
        auto ee = b.create();
        // Fuck you, shitty LLVM ownership semantics.
        if (ee)
            main.release();
        auto result = ee->runFunction(ee->FindFunctionNamed(name.c_str()), std::vector<llvm::GenericValue>());
        auto intval = result.IntVal.getLimitedValue();
        if (!intval)
            throw std::runtime_error("Test returned false.");
        delete ee;
    });
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::AST::Module* root) {        
        auto m = a.GetGlobalModule()->AccessMember("Main", a, Wide::Lexer::Range(loc));
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->Resolve(nullptr).t);
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve(std::vector<Wide::Semantic::Type*>(), a));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        name = f->GetName();
        f->BuildCall(Wide::Semantic::ConcreteExpression(), std::vector<Wide::Semantic::ConcreteExpression>(), a, loc);
    }, g, files);
}

TEST_CASE("", "") {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    CHECK_NOTHROW(Compile(clangopts, { "IntegerOperations.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "PrimitiveADL.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "RecursiveTypeInference.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "CorecursiveTypeInference.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "MemberCall.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "AcceptQualifiedThis.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "DeferredVariable.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "DeferredLambda.wide" }));
    CHECK_NOTHROW(Compile(clangopts, { "DeferredExpressionStatement.wide" }));
    CHECK_NOTHROW(Compile(clangopts,  { "BooleanOperations.wide" }));
    CHECK_NOTHROW(Interpret(clangopts, { "BooleanShortCircuit.wide" }));
    CHECK_THROWS(Compile(clangopts, { "RejectQualifiedThis.wide" }));
    CHECK_THROWS(Compile(clangopts, { "SubmoduleNoQualifiedLookup.wide" }));
}