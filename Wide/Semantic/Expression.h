#pragma once 

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Range.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <memory>
#include <boost/signals2.hpp>
#include <boost/signals2/connection.hpp>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ADT/APInt.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        struct Type;
        struct CodegenContext;
        enum Change {
            Contents,
            Destroyed
        };
        struct Context;
        struct Statement {
            virtual void GenerateCode(CodegenContext& con) = 0;
            virtual void Instantiate() = 0;
        };
        struct Expression : public Statement {
            static void AddDefaultHandlers(Analyzer& a);
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(CodegenContext& con);
            bool IsConstant();
            boost::signals2::signal<void(Expression*)> OnChanged;
        private:
            void Instantiate();
            std::unordered_map<llvm::Function*, llvm::Value*> values;
            void GenerateCode(CodegenContext& con) override final {
                GetValue(con);
            }
            virtual bool IsConstantExpression() = 0; // If not constant then false
            virtual llvm::Value* ComputeValue(CodegenContext& con) = 0;
        };
    }
}
namespace Wide {
    namespace Semantic {
        struct CodegenContext {
            CodegenContext(const CodegenContext&) = default;
            struct EHScope {
                CodegenContext* context;
                llvm::BasicBlock* target;
                llvm::PHINode* phi;
                std::vector<llvm::Constant*> types;
            };

            operator llvm::LLVMContext&() { return module->getContext(); }
            llvm::IRBuilder<>* operator->() { return insert_builder; }
            operator llvm::Module*() { return module; }

            std::list<std::pair<std::function<void(CodegenContext&)>, bool>> GetAddedDestructors(CodegenContext& other) {
                return std::list<std::pair<std::function<void(CodegenContext&)>, bool>>(std::next(other.Destructors.begin(), Destructors.size()), other.Destructors.end());
            }
            void GenerateCodeAndDestroyLocals(std::function<void(CodegenContext&)> action);
            void DestroyDifference(CodegenContext& other, bool EH);
            void DestroyAll(bool EH);
            void DestroyTillLastTry();
            bool IsTerminated(llvm::BasicBlock* bb);

            llvm::BasicBlock* GetUnreachableBlock();
            llvm::Type* GetLpadType();
            llvm::Function* GetEHPersonality();
            llvm::Function* GetCXABeginCatch();
            llvm::Function* GetCXAEndCatch();
            llvm::Function* GetCXARethrow();
            llvm::Function* GetCXAThrow();
            llvm::Function* GetCXAAllocateException();
            llvm::Function* GetCXAFreeException();
            llvm::IntegerType* GetPointerSizedIntegerType();
            llvm::PointerType* GetInt8PtrTy();
            llvm::Instruction* GetAllocaInsertPoint();

            llvm::AllocaInst* CreateAlloca(Type* t);
            llvm::Value* CreateStructGEP(llvm::Value* v, unsigned num);

            llvm::BasicBlock* CreateLandingpadForEH();

            bool destructing = false;
            bool catching = false;
            llvm::Module* module;
            // Mostly used for e.g. member variables.
            Wide::Util::optional<EHScope> EHHandler;
        private:
            CodegenContext(llvm::Module* mod, llvm::IRBuilder<>& alloc_builder, llvm::IRBuilder<>& gep_builder, llvm::IRBuilder<>& ir_builder);
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>> Destructors;
            llvm::IRBuilder<>* alloca_builder;
            llvm::IRBuilder<>* insert_builder;
            llvm::IRBuilder<>* gep_builder;
            std::shared_ptr<std::unordered_map<llvm::AllocaInst*, std::unordered_map<unsigned, llvm::Value*>>> gep_map;
        public:
            bool HasDestructors();
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator AddDestructor(std::function<void(CodegenContext&)>);
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator AddExceptionOnlyDestructor(std::function<void(CodegenContext&)>);
            void EraseDestructor(std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator it);
            void AddDestructors(std::list<std::pair<std::function<void(CodegenContext&)>, bool>>);
            static void EmitFunctionBody(llvm::Function* func, std::function<void(CodegenContext&)> body);
        };
        struct SourceExpression : public Expression {
            struct ExpressionData {
                boost::optional<Type*> type;
                boost::signals2::scoped_connection connection;
                ExpressionData(boost::optional<Type*> type, boost::signals2::scoped_connection connection)
                    : type(std::move(type)), connection(std::move(connection)) {}
                ExpressionData(ExpressionData&& other) = default;
                ExpressionData& operator=(ExpressionData&& other) = default;
            };
        private:
            std::unordered_map<std::shared_ptr<Expression>, ExpressionData> exprs;
            boost::optional<Type*> curr_type;
        public:
            SourceExpression(Wide::Range::Erased<std::shared_ptr<Expression>> exprs);
            SourceExpression(const SourceExpression&) = delete;
            Type* GetType() override final;
            bool IsConstantExpression() override final;

            virtual Type* CalculateType() = 0;
        };
        struct ResultExpression : public SourceExpression {
        private:
            struct Result {
                std::shared_ptr<Expression> expr;
                boost::signals2::scoped_connection connection;
            };
            boost::optional<Result> result;
        public:
            ResultExpression(Wide::Range::Erased<std::shared_ptr<Expression>> exprs);
            virtual std::shared_ptr<Expression> CalculateResult() = 0;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext&) override final;
        };

        struct ImplicitLoadExpr : public SourceExpression {
            ImplicitLoadExpr(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> src;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitStoreExpr : public SourceExpression {
            ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value);
            std::shared_ptr<Expression> mem, val;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };
        
        struct LvalueCast : public SourceExpression {
            LvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct RvalueCast : public SourceExpression {
            RvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        
        struct Chain : SourceExpression {
            Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result);
            std::shared_ptr<Expression> SideEffect;
            std::shared_ptr<Expression> result;
            Type* CalculateType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };
        
        struct ConstantExpression : Expression {
            bool IsConstantExpression() override final { return true; }
        };

        struct String : ConstantExpression {
            String(std::string s, Analyzer& an);
            std::string str;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Integer : ConstantExpression {
            Integer(llvm::APInt val, Analyzer& an);
            llvm::APInt value;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Boolean : ConstantExpression {
            Boolean(bool b, Analyzer& a);
            bool b;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        std::shared_ptr<Expression> CreateResultExpression(Wide::Range::Erased<std::shared_ptr<Expression>> dependents, std::function<std::shared_ptr<Expression>()> func);
        std::shared_ptr<Expression> CreatePrimUnOp(std::shared_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimGlobal(Wide::Range::Erased<std::shared_ptr<Expression>> dependents, Type* ret, std::function<llvm::Value*(CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimGlobal(Wide::Range::Erased<std::shared_ptr<Expression>> dependents, Analyzer& a, std::function<void(CodegenContext&)>);
        std::shared_ptr<Expression> BuildValue(std::shared_ptr<Expression>);
        std::shared_ptr<Expression> BuildChain(std::shared_ptr<Expression>, std::shared_ptr<Expression>);
        std::shared_ptr<Expression> CreateTemporary(Type* t, Context c);
        std::shared_ptr<Expression> CreateAddressOf(std::shared_ptr<Expression> expr, Context c);
        std::shared_ptr<Expression> CreateErrorExpression(std::unique_ptr<Wide::Semantic::Error> err);
    }
}