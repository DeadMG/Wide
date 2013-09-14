#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Catch.h>

namespace Wide {
    namespace Codegen {
        class MockFunction : public Function {
        public:
            void AddStatement(Codegen::Statement*) {}
        };
        class MockIntegralExpression : public IntegralExpression {
            unsigned long long value;
            bool sign;
        public:
            MockIntegralExpression(unsigned long long val, bool s)
                : value(val), sign(s) {}
            unsigned long long GetValue() { return value; }
            bool GetSign() { return sign; }
        };
        class MockFunctionValue : public FunctionValue {
            std::string name;
        public:
            MockFunctionValue(std::string nam)
                : name(std::move(nam)) {}
            std::string GetMangledName() { return name; }
        };
        class MockStringExpression : public StringExpression {
            std::string val;
        public:
            MockStringExpression(std::string value)
                : val(std::move(value)) {}
            std::string GetContents() { return val; }
        };
        class MockGenerator : public Generator {
        public:
            MockGenerator(llvm::DataLayout data)
                : layout(data) {}
            llvm::LLVMContext con;
            llvm::DataLayout layout;

            virtual llvm::LLVMContext& GetContext() { return con; }
            virtual llvm::DataLayout GetDataLayout() { return layout; }
            virtual void AddEliminateType(llvm::Type* t) {}
            virtual void AddClangTU(std::function<void(llvm::Module*)>) {}
            virtual std::size_t GetInt8AllocSize() { return 1; }

            virtual Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) { return new MockFunction; }
            virtual Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) { return nullptr; }
            virtual FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) { return nullptr; }
            virtual StringExpression* CreateStringExpression(std::string val) { return new MockStringExpression(std::move(val)); }
            virtual NamedGlobalVariable* CreateGlobalVariable(std::string) { return nullptr; }
            virtual StoreExpression* CreateStore(Expression*, Expression*) { return nullptr; }
            virtual LoadExpression* CreateLoad(Expression*) { return nullptr; }
            virtual ReturnStatement* CreateReturn() { return nullptr; }
            virtual ReturnStatement* CreateReturn(std::function < Expression*()>) { return nullptr; }
            virtual ReturnStatement* CreateReturn(Expression*) { return nullptr; }
            virtual FunctionValue* CreateFunctionValue(std::string name) { return new MockFunctionValue(std::move(name)); }
            virtual IntegralExpression* CreateIntegralExpression(unsigned long long val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) { return new MockIntegralExpression(val, is_signed); }
            virtual ChainExpression* CreateChainExpression(Statement*, Expression*) { return nullptr; }
            virtual FieldExpression* CreateFieldExpression(Expression*, unsigned) { return nullptr; }
            virtual FieldExpression* CreateFieldExpression(Expression*, std::function<unsigned()>) { return nullptr; }
            virtual ParamExpression* CreateParameterExpression(unsigned) { return nullptr; }
            virtual ParamExpression* CreateParameterExpression(std::function<unsigned()>) { return nullptr; }
            virtual IfStatement* CreateIfStatement(Expression*, Statement*, Statement*) { return nullptr; }
            virtual IfStatement* CreateIfStatement(std::function<Expression*()>, Statement*, Statement*) { return nullptr; }
            virtual ChainStatement* CreateChainStatement(Statement*, Statement*) { return nullptr; }
            virtual TruncateExpression* CreateTruncate(Expression*, std::function<llvm::Type*(llvm::Module*)>) { return nullptr; }
            virtual WhileStatement* CreateWhile(std::function<Expression*()>, Statement*) { return nullptr; }
            virtual WhileStatement* CreateWhile(Expression*, Statement*) { return nullptr; }
            virtual NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type) { return nullptr; }
            virtual IntegralLeftShiftExpression* CreateLeftShift(Expression*, Expression*) { return nullptr; }
            virtual IntegralRightShiftExpression* CreateRightShift(Expression*, Expression*, bool) { return nullptr; }
            virtual IntegralLessThan* CreateLT(Expression* lhs, Expression* rhs, bool) { return nullptr; }
            virtual ZExt* CreateZeroExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual NegateExpression* CreateNegateExpression(Expression* val) { return nullptr; }
            virtual OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual EqualityExpression* CreateEqualityExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual PlusExpression* CreatePlusExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual MultiplyExpression* CreateMultiplyExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual SExt* CreateSignedExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual IsNullExpression* CreateIsNullExpression(Expression* val) { return nullptr; }
            virtual SubExpression* CreateSubExpression(Expression* l, Expression* r) { return nullptr; }
            virtual XorExpression* CreateXorExpression(Expression* l, Expression* r) { return nullptr; }
            virtual ModExpression* CreateModExpression(Expression* l, Expression* r, bool is_signed) { return nullptr; }
            virtual DivExpression* CreateDivExpression(Expression* l, Expression* r, bool is_signed) { return nullptr; }  
            virtual FPLessThan* CreateFPLT(Expression* l, Expression* r) { return nullptr; }
            virtual FPDiv* CreateFPDiv(Expression* l, Expression* r) { return nullptr; }
            virtual FPMod* CreateFPMod(Expression* l, Expression* r) { return nullptr; }
            virtual FPExtension* CreateFPExtension(Expression*, std::function < llvm::Type*(llvm::Module*)>) { return nullptr;  }
        };
    }
}


#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/ParallelForEach.h>
#include <Wide/Parser/Builder.h>
#include <Wide/Util/Ranges/IStreamRange.h>
#include <Wide/Util/ConcurrentVector.h>
#include <mutex>
#include <atomic>
#include <sstream>
#include <fstream>
#include <initializer_list>
#include <iostream>

#ifndef _MSC_VER
#include <llvm/Support/Host.h>
#endif


void Compile(const Wide::Options::Clang& copts, llvm::DataLayout lopts, std::initializer_list<std::string> files) {
    Wide::Codegen::MockGenerator Generator(lopts);
    Wide::AST::Combiner combiner;
    Wide::Semantic::Analyzer Sema(copts, &Generator);

    Wide::Concurrency::Vector<std::string> excepts;
    Wide::Concurrency::Vector<std::string> warnings;
    Wide::Concurrency::Vector<std::shared_ptr<Wide::AST::Builder>> builders;
    Wide::Concurrency::ParallelForEach(files.begin(), files.end(), [&](const std::string& filename) {
        std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
        if (!inputfile)
            throw std::runtime_error("Could not open this input file.");
        std::noskipws(inputfile);
        Wide::Lexer::Arguments largs;
        auto contents = Wide::Range::IStreamRange(inputfile);
        Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents);
        auto parsererrorhandler = [&](Wide::Lexer::Range where, Wide::Parser::Error what) {
            std::stringstream str;
            str << "Error in file " << filename << ", line " << where.begin.line << " column " << where.begin.column << ":\n";
            str << Wide::Parser::ErrorStrings.at(what);
            excepts.push_back(str.str());
        };
        auto parserwarninghandler = [&](Wide::Lexer::Range where, Wide::Parser::Warning what) {
            std::stringstream str;
            str << "Warning in file " << filename << ", line " << where.begin.line << " column " << where.begin.column << ":\n";
            str << Wide::Parser::WarningStrings.at(what);
            warnings.push_back(str.str());
        };
        try {
            auto builder = std::make_shared<Wide::AST::Builder>(parsererrorhandler, parserwarninghandler);
            Wide::Parser::ParseGlobalModuleContents(lex, *builder, builder->GetGlobalModule());
            builders.push_back(std::move(builder));
        }
        catch (Wide::Parser::ParserError& e) {
            parsererrorhandler(e.where(), e.error());
        }
        catch (std::exception& e) {
            excepts.push_back(e.what());
        }
        catch (...) {
            excepts.push_back("Internal Compiler Error");
        }
    });

    for (auto&& x : warnings)
        std::cout << x << "\n";

    if (excepts.empty()) {
        for (auto&& x : builders)
            combiner.Add(x->GetGlobalModule());
        Sema(combiner.GetGlobalModule());
    } else {
        for (auto&& msg : excepts) {
            std::cout << msg << "\n";
        }
        throw std::runtime_error("Terminating test due to failures.");
    }
}

TEST_CASE("", "") {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(clangopts.TargetOptions.Triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(clangopts.TargetOptions.Triple, llvm::Triple(clangopts.TargetOptions.Triple).getArchName(), "", targetopts));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "IntegerOperations.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "PrimitiveADL.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "RecursiveTypeInference.wide" }));
    CHECK_NOTHROW(Compile(clangopts, *targetmachine->getDataLayout(), { "CorecursiveTypeInference.wide" }));
}