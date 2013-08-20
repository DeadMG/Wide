#pragma once
#include <vector>
#include <Wide/Codegen/Generator.h>

namespace llvm {
    class Value;
#ifdef _MSC_VER
    template <bool preserveNames = true>
        class IRBuilderDefaultInserter;
    class ConstantFolder;
    template<bool preserveNames = true, typename T = ConstantFolder,
        typename Inserter = IRBuilderDefaultInserter<preserveNames> >
    class IRBuilder;
#else
}
#include <llvm/IR/IRBuilder.h>
namespace llvm {
#endif
    class Module;
}

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
            LLVMCodegen::Expression* val;
        public:
            Codegen::Expression* GetReturnExpression();
            ReturnStatement();
            ReturnStatement(LLVMCodegen::Expression* e);
            void Build(llvm::IRBuilder<>& bb, Generator& g);
        };

        class IfStatement : public Statement, public Codegen::IfStatement {
            LLVMCodegen::Statement* true_br;
            LLVMCodegen::Statement* false_br;
            LLVMCodegen::Expression* condition;
        public:
            IfStatement(LLVMCodegen::Expression* cond, LLVMCodegen::Statement* tbr, LLVMCodegen::Statement* fbr)
                : condition(cond), true_br(std::move(tbr)), false_br(std::move(fbr)) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g);
        };

        class ChainStatement : public Statement, public Codegen::ChainStatement {
            LLVMCodegen::Statement* lhs;
            LLVMCodegen::Statement* rhs;
        public:
            ChainStatement(LLVMCodegen::Statement* l, LLVMCodegen::Statement* r)
                : lhs(l), rhs(r) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g) {
                lhs->Build(bb, g);
                rhs->Build(bb, g);
            }
        };
        class WhileStatement : public Statement, public Codegen::WhileStatement {
            LLVMCodegen::Expression* cond;
            LLVMCodegen::Statement* body;
        public:
            WhileStatement(LLVMCodegen::Expression* c, LLVMCodegen::Statement* b)
                : cond(c), body(b) {}
            void Build(llvm::IRBuilder<>& b, Generator& g);
        };
    }
}