#pragma once

#include <Wide/Codegen/Generator.h>
#include <unordered_map>
#include <vector>
#include <memory>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)


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

            MockFunction mockfunc;


            llvm::LLVMContext con;
            llvm::DataLayout layout;

            std::unordered_map<std::string, std::unique_ptr<MockStringExpression>> mockstrings;
            std::unordered_map<std::string, std::unique_ptr<MockFunctionValue>> mockfunctions;
            std::vector<std::unique_ptr<MockIntegralExpression>> mockintegers;

            virtual llvm::LLVMContext& GetContext() { return con; }
            virtual llvm::DataLayout GetDataLayout() { return layout; }
            virtual void AddEliminateType(llvm::Type* t) {}
            virtual void AddClangTU(std::function<void(llvm::Module*)>) {}
            virtual std::size_t GetInt8AllocSize() { return 1; }

            virtual Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) { return &mockfunc; }
            virtual Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) { return nullptr; }
            virtual FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) { return nullptr; }
            virtual StringExpression* CreateStringExpression(std::string val) {
                if (mockstrings.find(val) != mockstrings.end())
                    return mockstrings[val].get();
                return (mockstrings[val] = std::unique_ptr<MockStringExpression>(new MockStringExpression(std::move(val)))).get();
            }
            virtual NamedGlobalVariable* CreateGlobalVariable(std::string) { return nullptr; }
            virtual StoreExpression* CreateStore(Expression*, Expression*) { return nullptr; }
            virtual LoadExpression* CreateLoad(Expression*) { return nullptr; }
            virtual ReturnStatement* CreateReturn() { return nullptr; }
            virtual ReturnStatement* CreateReturn(std::function < Expression*()>) { return nullptr; }
            virtual ReturnStatement* CreateReturn(Expression*) { return nullptr; }
            virtual FunctionValue* CreateFunctionValue(std::string val) {
                if (mockfunctions.find(val) != mockfunctions.end())
                    return mockfunctions[val].get();
                return (mockfunctions[val] = std::unique_ptr<MockFunctionValue>(new MockFunctionValue(std::move(val)))).get();
            }
            virtual IntegralExpression* CreateIntegralExpression(unsigned long long val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) { 
                mockintegers.push_back(std::unique_ptr<MockIntegralExpression>(new MockIntegralExpression(val, is_signed)));
                return mockintegers.back().get();
            }
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
            virtual Nop* CreateNop() { return nullptr; }
            virtual Deferred* CreateDeferredStatement(std::function<Statement*()>) { return nullptr; }
        };
    }
}