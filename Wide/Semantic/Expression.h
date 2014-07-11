#pragma once 

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
            ImplicitLoadExpr(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> src;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitStoreExpr : public Expression {
            ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value);
            std::shared_ptr<Expression> mem, val;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitTemporaryExpr : public Expression {
            ImplicitTemporaryExpr(Type* what, Context c);
            Type* of;
            llvm::Value* alloc;
            Context c;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct LvalueCast : public Expression {
            LvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct RvalueCast : public Expression {
            RvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitAddressOf : public Expression {
            ImplicitAddressOf(std::shared_ptr<Expression>, Context c);
            Context c;
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct String : Expression {
            String(std::string s, Analyzer& an);
            std::string str;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Integer : Expression {
            Integer(llvm::APInt val, Analyzer& an);
            llvm::APInt value;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Boolean : Expression {
            Boolean(bool b, Analyzer& a);
            bool b;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Chain : Expression {
            Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result);
            std::shared_ptr<Expression> SideEffect;
            std::shared_ptr<Expression> result;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
            Expression* GetImplementation() override final;
        };

        std::shared_ptr<Expression> CreatePrimUnOp(std::shared_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimGlobal(Type* ret, std::function<llvm::Value*(CodegenContext&)>);
        std::shared_ptr<Expression> BuildValue(std::shared_ptr<Expression>);
        std::shared_ptr<Expression> BuildChain(std::shared_ptr<Expression>, std::shared_ptr<Expression>);
    }
}