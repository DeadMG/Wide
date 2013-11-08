#include <Wide/Util/Driver/Compile.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMGenerator.h>

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Test/Catch.h>

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

void Interpret(const Wide::Options::Clang& copts, std::string file) {
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    Wide::Options::LLVM llvmopts;
    Wide::LLVMCodegen::Generator g(llvmopts, copts.TargetOptions.Triple, std::function<void(std::unique_ptr<llvm::Module>)>());
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
    }, g, { file });
}

TEST_CASE("Compile Success", "Compile") {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    CHECK_NOTHROW(Compile(clangopts, "IntegerOperations.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "PrimitiveADL.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "RecursiveTypeInference.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "CorecursiveTypeInference.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "MemberCall.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "AcceptQualifiedThis.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "DeferredVariable.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "DeferredLambda.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "DeferredExpressionStatement.wide" ));
    CHECK_NOTHROW(Compile(clangopts, "BooleanOperations.wide" ));
}

TEST_CASE("Interpet Success", "Interpret") {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    CHECK_NOTHROW(Interpret(clangopts, "BooleanShortCircuit.wide" ));
    CHECK_NOTHROW(Interpret(clangopts, "ClangADL.wide" ));
    CHECK_NOTHROW(Interpret(clangopts, "ClangMixedADL.wide" ));
    CHECK_NOTHROW(Interpret(clangopts, "ClangMemberFunction.wide" ));
    CHECK_NOTHROW(Interpret(clangopts, "ClangMemberOperator.wide" ));
}

TEST_CASE("Compile Fail", "Compile") {
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    CHECK_NOTHROW(Compile(clangopts, "RejectNontypeFunctionArgumentExpression.wide", Wide::Semantic::Error::ExpressionNoType, 2, 3, 2, 9 ));
    CHECK_NOTHROW(Compile(clangopts, "SubmoduleNoQualifiedLookup.wide", Wide::Semantic::Error::NoMember, 5, 9, 5, 28 ));
    CHECK_THROWS(Compile(clangopts, "RejectLvalueQualifiedThis.wide" ));
    CHECK_THROWS(Compile(clangopts, "RejectRvalueQualifiedThis.wide" ));
}