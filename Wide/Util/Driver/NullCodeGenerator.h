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
    namespace Driver {
        class NullFunction : public Codegen::Function {
        public:
            void AddStatement(Codegen::Statement*) {}
        };
        class NullWhile : public Codegen::WhileStatement {
        public:
            void SetBody(Codegen::Statement*) {}
        };
        class NullIntegralExpression : public Codegen::IntegralExpression {
            unsigned long long value;
            bool sign;
        public:
            NullIntegralExpression(unsigned long long val, bool s)
                : value(val), sign(s) {}
            unsigned long long GetValue() { return value; }
            bool GetSign() { return sign; }
        };
        class NullFunctionValue : public Codegen::FunctionValue {
            std::string name;
        public:
            NullFunctionValue(std::string nam)
                : name(std::move(nam)) {}
            std::string GetMangledName() { return name; }
        };
        class NullStringExpression : public Codegen::StringExpression {
            std::string val;
        public:
            NullStringExpression(std::string value)
                : val(std::move(value)) {}
            std::string GetContents() { return val; }
        };
        class NullGenerator : public Codegen::Generator {
        public:
            NullGenerator(std::string triple);

            NullFunction mockfunc;
            NullWhile mockwhile;

            llvm::LLVMContext con;
            std::string layout;

            std::unordered_map<std::string, std::unique_ptr<NullStringExpression>> mockstrings;
            std::unordered_map<std::string, std::unique_ptr<NullFunctionValue>> mockfunctions;
            std::vector<std::unique_ptr<NullIntegralExpression>> mockintegers;

            virtual llvm::LLVMContext& GetContext() { return con; }
            virtual llvm::DataLayout GetDataLayout() { return llvm::DataLayout(layout); }
            virtual void AddEliminateType(llvm::Type* t) {}
            virtual void AddClangTU(std::function<void(llvm::Module*)>) {}
            virtual std::size_t GetInt8AllocSize() { return 1; }

            virtual Codegen::DeferredExpr* CreateDeferredExpression(std::function<Codegen::Expression*()>) { return nullptr; }
            virtual Codegen::Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) { return &mockfunc; }
            virtual Codegen::Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) { return nullptr; }
            virtual Codegen::FunctionCall* CreateFunctionCall(Codegen::Expression*, std::vector<Codegen::Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) { return nullptr; }
            virtual Codegen::StringExpression* CreateStringExpression(std::string val) {
                if (mockstrings.find(val) != mockstrings.end())
                    return mockstrings[val].get();
                return (mockstrings[val] = std::unique_ptr<NullStringExpression>(new NullStringExpression(std::move(val)))).get();
            }
            virtual Codegen::NamedGlobalVariable* CreateGlobalVariable(std::string) { return nullptr; }
            virtual Codegen::StoreExpression* CreateStore(Codegen::Expression*, Codegen::Expression*) { return nullptr; }
            virtual Codegen::LoadExpression* CreateLoad(Codegen::Expression*) { return nullptr; }
            virtual Codegen::ReturnStatement* CreateReturn() { return nullptr; }
            virtual Codegen::ReturnStatement* CreateReturn(std::function<Codegen::Expression*()>) { return nullptr; }
            virtual Codegen::ReturnStatement* CreateReturn(Codegen::Expression*) { return nullptr; }
            virtual Codegen::FunctionValue* CreateFunctionValue(std::string val) {
                if (mockfunctions.find(val) != mockfunctions.end())
                    return mockfunctions[val].get();
                return (mockfunctions[val] = std::unique_ptr<NullFunctionValue>(new NullFunctionValue(std::move(val)))).get();
            }
            virtual Codegen::IntegralExpression* CreateIntegralExpression(std::uint64_t val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) { 
                mockintegers.push_back(std::unique_ptr<NullIntegralExpression>(new NullIntegralExpression(val, is_signed)));
                return mockintegers.back().get();
            }
            virtual Codegen::Continue* CreateContinue(Codegen::WhileStatement* s) { return nullptr; }
            virtual Codegen::Break* CreateBreak(Codegen::WhileStatement* s) { return nullptr; }
            virtual Codegen::ChainExpression* CreateChainExpression(Codegen::Statement*, Codegen::Expression*) { return nullptr; }
            virtual Codegen::FieldExpression* CreateFieldExpression(Codegen::Expression*, unsigned) { return nullptr; }
            virtual Codegen::FieldExpression* CreateFieldExpression(Codegen::Expression*, std::function<unsigned()>) { return nullptr; }
            virtual Codegen::ParamExpression* CreateParameterExpression(unsigned) { return nullptr; }
            virtual Codegen::ParamExpression* CreateParameterExpression(std::function<unsigned()>) { return nullptr; }
            virtual Codegen::IfStatement* CreateIfStatement(Codegen::Expression*, Codegen::Statement*, Codegen::Statement*) { return nullptr; }
            virtual Codegen::IfStatement* CreateIfStatement(std::function<Codegen::Expression*()>, Codegen::Statement*, Codegen::Statement*) { return nullptr; }
            virtual Codegen::ChainStatement* CreateChainStatement(Codegen::Statement*,Codegen:: Statement*) { return nullptr; }
            virtual Codegen::TruncateExpression* CreateTruncate(Codegen::Expression*, std::function<llvm::Type*(llvm::Module*)>) { return nullptr; }
            virtual Codegen::WhileStatement* CreateWhile(std::function<Codegen::Expression*()>) { return &mockwhile; }
            virtual Codegen::WhileStatement* CreateWhile(Codegen::Expression*) { return &mockwhile; }
            virtual Codegen::NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type) { return nullptr; }
            virtual Codegen::IntegralLeftShiftExpression* CreateLeftShift(Codegen::Expression*, Codegen::Expression*) { return nullptr; }
            virtual Codegen::IntegralRightShiftExpression* CreateRightShift(Codegen::Expression*, Codegen::Expression*, bool) { return nullptr; }
            virtual Codegen::IntegralLessThan* CreateLT(Codegen::Expression* lhs, Codegen::Expression* rhs, bool) { return nullptr; }
            virtual Codegen::ZExt* CreateZeroExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual Codegen::NegateExpression* CreateNegateExpression(Codegen::Expression* val) { return nullptr; }
            virtual Codegen::OrExpression* CreateOrExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) { return nullptr; }
            virtual Codegen::EqualityExpression* CreateEqualityExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) { return nullptr; }
            virtual Codegen::PlusExpression* CreatePlusExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) { return nullptr; }
            virtual Codegen::MultiplyExpression* CreateMultiplyExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) { return nullptr; }
            virtual Codegen::AndExpression* CreateAndExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) { return nullptr; }
            virtual Codegen::SExt* CreateSignedExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual Codegen::IsNullExpression* CreateIsNullExpression(Codegen::Expression* val) { return nullptr; }
            virtual Codegen::SubExpression* CreateSubExpression(Codegen::Expression* l, Codegen::Expression* r) { return nullptr; }
            virtual Codegen::XorExpression* CreateXorExpression(Codegen::Expression* l, Codegen::Expression* r) { return nullptr; }
            virtual Codegen::ModExpression* CreateModExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) { return nullptr; }
            virtual Codegen::DivExpression* CreateDivExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) { return nullptr; }  
            virtual Codegen::FPLessThan* CreateFPLT(Codegen::Expression* l, Codegen::Expression* r) { return nullptr; }
            virtual Codegen::FPDiv* CreateFPDiv(Codegen::Expression* l, Codegen::Expression* r) { return nullptr; }
            virtual Codegen::FPMod* CreateFPMod(Codegen::Expression* l, Codegen::Expression* r) { return nullptr; }
            virtual Codegen::FPExtension* CreateFPExtension(Codegen::Expression*, std::function < llvm::Type*(llvm::Module*)>) { return nullptr;  }
            virtual Codegen::Nop* CreateNop() { return nullptr; }
            virtual Codegen::Deferred* CreateDeferredStatement(std::function<Codegen::Statement*()>) { return nullptr; }
        };
    }
}