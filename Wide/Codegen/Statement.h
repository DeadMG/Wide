#pragma once
#include <vector>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#pragma warning(pop)

namespace Wide {
    namespace LLVMCodegen {
        class Generator;
        struct Expression;
        class Statement {
        public:
            virtual ~Statement() {}
            virtual void Build(llvm::IRBuilder<>& bb, Generator& g) = 0;
        };
        class ReturnStatement : public Statement, public Codegen::ReturnStatement {
            std::function<LLVMCodegen::Expression*()> val;
        public:
            Codegen::Expression* GetReturnExpression();
            ReturnStatement();
            ReturnStatement(std::function<LLVMCodegen::Expression*()> e);
            void Build(llvm::IRBuilder<>& bb, Generator& g) override final;
        };

        class Deferred : public Statement, public Codegen::Deferred {
            std::function<Codegen::Statement*()> func;
        public:
            Deferred(std::function<Codegen::Statement*()> f)
                : func(std::move(f)) {}
            void Build(llvm::IRBuilder<>&, Generator& g) override final;
        };

        class IfStatement : public Statement, public Codegen::IfStatement {
            LLVMCodegen::Statement* true_br;
            LLVMCodegen::Statement* false_br;
            std::function<LLVMCodegen::Expression*()> condition;
        public:
            IfStatement(std::function<LLVMCodegen::Expression*()> cond, LLVMCodegen::Statement* tbr, LLVMCodegen::Statement* fbr)
                : true_br(std::move(tbr)), false_br(std::move(fbr)), condition(cond) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g) override final;
        };

        class ChainStatement : public Statement, public Codegen::ChainStatement {
            LLVMCodegen::Statement* lhs;
            LLVMCodegen::Statement* rhs;
        public:
            ChainStatement(LLVMCodegen::Statement* l, LLVMCodegen::Statement* r)
                : lhs(l), rhs(r) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g) override final {
                lhs->Build(bb, g);
                rhs->Build(bb, g);
            }
        };
        class WhileStatement : public Statement, public Codegen::WhileStatement {
            std::function<LLVMCodegen::Expression*()> cond;
            LLVMCodegen::Statement* body;
        public:
            WhileStatement(std::function<LLVMCodegen::Expression*()> c, LLVMCodegen::Statement* b)
                : cond(c), body(b) {}
            void Build(llvm::IRBuilder<>& b, Generator& g) override final;
        };
    }
}