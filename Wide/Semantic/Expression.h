
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <memory>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/APInt.h>
#pragma warning(pop)

namespace Wide {
    namespace Codegen {
        class Generator;
    }
    namespace Semantic {
        struct Context;
        struct ImplicitLoadExpr : public Expression {
            ImplicitLoadExpr(std::unique_ptr<Expression> expr);
            std::unique_ptr<Expression> src;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct ImplicitStoreExpr : public Expression {
            ImplicitStoreExpr(std::unique_ptr<Expression> memory, std::unique_ptr<Expression> value);
            std::unique_ptr<Expression> mem, val;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct ImplicitTemporaryExpr : public Expression {
            ImplicitTemporaryExpr(Type* what, Context c);
            Type* of;
            llvm::Value* alloc;
            Context c;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct LvalueCast : public Expression {
            LvalueCast(std::unique_ptr<Expression> expr);
            std::unique_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct RvalueCast : public Expression {
            RvalueCast(std::unique_ptr<Expression> expr);
            std::unique_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct ExpressionReference : public Expression {
            ExpressionReference(Expression* e);
            Expression* expr;
            void OnNodeChanged(Node* n, Change what) override final;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            Expression* GetImplementation() override final;
        };

        struct ImplicitAddressOf : public Expression {
            ImplicitAddressOf(std::unique_ptr<Expression>);
            std::unique_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct String : Expression {
            String(std::string s, Analyzer& an);
            std::string str;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct Integer : Expression {
            Integer(llvm::APInt val, Analyzer& an);
            llvm::APInt value;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        struct Boolean : Expression {
            Boolean(bool b, Analyzer& a);
            bool b;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final;
        };

        std::unique_ptr<Expression> CreatePrimUnOp(std::unique_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)>);
        std::unique_ptr<Expression> CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)>);
        std::unique_ptr<Expression> CreatePrimAssOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)>);
        std::unique_ptr<Expression> CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)>);
        std::unique_ptr<Expression> BuildValue(std::unique_ptr<Expression>);
        std::unique_ptr<Expression> BuildChain(std::unique_ptr<Expression>, std::unique_ptr<Expression>);
    }
}