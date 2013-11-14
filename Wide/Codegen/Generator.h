#pragma once
#include <functional>
#include <vector>
#include <string>

namespace llvm {
    class Type;
    class Module;
    class LLVMContext;
    class DataLayout;
}

namespace Wide {
    namespace Semantic {
        class Function;
    }
    namespace Codegen {
        class Statement { public: virtual ~Statement() {} };
        class Expression : public Statement {};
        class Variable : public Expression {};
        class FunctionCall : public Expression {};
        class StringExpression  : public Expression{
        public:
            virtual std::string GetContents() = 0;
        };
        class NamedGlobalVariable : public Expression {};
        class StoreExpression : public Expression {};
        class LoadExpression : public Expression {};
        class ReturnStatement  : public Statement {};
        class FunctionValue : public Expression {
        public:
            virtual std::string GetMangledName() = 0;
        };
        class IntegralExpression : public Expression {
        public:
            virtual unsigned long long GetValue() = 0;
            virtual bool GetSign() = 0;
        };
        class ChainExpression : public Expression {};
        class FieldExpression : public Expression {};
        class ParamExpression : public Expression {};
        class IfStatement  : public Statement{};
        class ChainStatement  : public Statement{};
        class TruncateExpression : public Expression {};
        class WhileStatement : public Statement{};
        class NullExpression : public Expression {};
        class IntegralLeftShiftExpression : public Expression {};
        class IntegralRightShiftExpression : public Expression {};
        class IntegralLessThan : public Expression {};
        class ZExt : public Expression {};
        class NegateExpression : public Expression {};
        class OrExpression : public Expression {};
        class EqualityExpression : public Expression {};
        class PlusExpression : public Expression {};
        class MultiplyExpression : public Expression {};
        class AndExpression : public Expression {};
        class SExt : public Expression {};
        class IsNullExpression : public Expression {};
        class SubExpression : public Expression {};
        class XorExpression : public Expression {};
        class ModExpression : public Expression {};
        class DivExpression : public Expression {};
        class FPExtension : public Expression {};
        class FPLessThan : public Expression {};
        class FPMod : public Expression {};
        class FPDiv : public Expression {};
        class Nop : public Expression {};
        class Deferred : public Statement {};
        class DeferredExpr : public Expression {};
        class Function {
        public:
            virtual ~Function() {}
            virtual void AddStatement(Statement* s) = 0;
        };
        class Generator {
        public:
            virtual Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) = 0;
            virtual Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) = 0;
            virtual FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) = 0;
            virtual Deferred* CreateDeferredStatement(std::function<Codegen::Statement*()>) = 0;
            virtual DeferredExpr* CreateDeferredExpression(std::function<Codegen::Expression*()>) = 0;
            virtual StringExpression* CreateStringExpression(std::string) = 0;
            virtual NamedGlobalVariable* CreateGlobalVariable(std::string) = 0;
            virtual StoreExpression* CreateStore(Expression*, Expression*) = 0;
            virtual LoadExpression* CreateLoad(Expression*) = 0;
            virtual ReturnStatement* CreateReturn() = 0;
            virtual ReturnStatement* CreateReturn(std::function<Expression*()>) = 0;
            virtual ReturnStatement* CreateReturn(Expression* e) {
                return CreateReturn([=] { return e; });
            }
            virtual Nop* CreateNop() = 0;
            virtual FunctionValue* CreateFunctionValue(std::string) = 0;
            virtual IntegralExpression* CreateIntegralExpression(unsigned long long val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) = 0;
            virtual ChainExpression* CreateChainExpression(Statement*, Expression*) = 0;
            virtual FieldExpression* CreateFieldExpression(Expression*, unsigned) = 0;
            virtual FieldExpression* CreateFieldExpression(Expression*, std::function<unsigned()>) = 0;
            virtual ParamExpression* CreateParameterExpression(unsigned) = 0;
            virtual ParamExpression* CreateParameterExpression(std::function<unsigned()>) = 0;
            virtual IfStatement* CreateIfStatement(Expression* expr, Statement* t, Statement* f) {
                return CreateIfStatement([=] { return expr; }, t, f);
            }
            virtual IfStatement* CreateIfStatement(std::function<Expression*()>, Statement*, Statement*) = 0;
            virtual ChainStatement* CreateChainStatement(Statement*, Statement*) = 0;
            virtual TruncateExpression* CreateTruncate(Expression*, std::function<llvm::Type*(llvm::Module*)>) = 0;
            virtual WhileStatement* CreateWhile(Expression* e, Statement* s) {
                return CreateWhile([=]{ return e; }, s);
            }
            virtual WhileStatement* CreateWhile(std::function<Expression*()>, Statement*) = 0;
            virtual NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type) = 0;
            virtual IntegralLeftShiftExpression* CreateLeftShift(Expression*, Expression*) = 0;
            virtual IntegralRightShiftExpression* CreateRightShift(Expression*, Expression*, bool) = 0;
            virtual IntegralLessThan* CreateLT(Expression* lhs, Expression* rhs, bool) = 0;
            virtual ZExt* CreateZeroExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) = 0;
            virtual NegateExpression* CreateNegateExpression(Expression* val) = 0;
            virtual OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs) = 0;
            virtual EqualityExpression* CreateEqualityExpression(Expression* lhs, Expression* rhs) = 0;
            virtual PlusExpression* CreatePlusExpression(Expression* lhs, Expression* rhs) = 0;
            virtual MultiplyExpression* CreateMultiplyExpression(Expression* lhs, Expression* rhs) = 0;
            virtual AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs) = 0;
            virtual SExt* CreateSignedExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) = 0;
            virtual IsNullExpression* CreateIsNullExpression(Expression* val) = 0;
            virtual SubExpression* CreateSubExpression(Expression* l, Expression* r) = 0;
            virtual XorExpression* CreateXorExpression(Expression* l, Expression* r) = 0;
            virtual ModExpression* CreateModExpression(Expression* l, Expression* r, bool is_signed) = 0;
            virtual DivExpression* CreateDivExpression(Expression* l, Expression* r, bool is_signed) = 0;
            virtual FPExtension* CreateFPExtension(Expression*, std::function<llvm::Type*(llvm::Module*)> to) = 0;
            virtual FPLessThan* CreateFPLT(Codegen::Expression* lhs, Codegen::Expression* rhs) = 0;
            virtual FPMod* CreateFPMod(Expression* lhs, Expression* rhs) = 0;
            virtual FPDiv* CreateFPDiv(Expression* lhs, Expression* rhs) = 0;

            virtual llvm::DataLayout GetDataLayout() = 0;
            virtual std::size_t GetInt8AllocSize() = 0;
            virtual void AddEliminateType(llvm::Type*) = 0;
            virtual llvm::LLVMContext& GetContext() = 0;

            virtual void AddClangTU(std::function<void(llvm::Module* m)>) = 0;

            virtual void operator()() {}
        };
    }
}