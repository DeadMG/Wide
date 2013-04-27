#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <vector>

namespace llvm {
    class Value;
    template <bool preserveNames = true>
        class IRBuilderDefaultInserter;
    class ConstantFolder;
    template<bool preserveNames = true, typename T = ConstantFolder,
        typename Inserter = IRBuilderDefaultInserter<preserveNames> >
    class IRBuilder;
    class Module;
}

namespace Wide {
    namespace Codegen {
        struct Expression;
        class Generator;
        class Statement {
        public:
            virtual ~Statement() {}
            virtual void Build(llvm::IRBuilder<>& bb, Generator& g) = 0;
        };

        class ReturnStatement : public Statement {
            Expression* val;
        public:
            Expression* GetReturnExpression();
            ReturnStatement();
            ReturnStatement(Expression* e);
            void Build(llvm::IRBuilder<>& bb, Generator& g);
        };

        class IfStatement : public Statement {
            Statement* true_br;
            Statement* false_br;
            Expression* condition;
        public:
            IfStatement(Expression* cond, Statement* tbr, Statement* fbr)
                : condition(cond), true_br(std::move(tbr)), false_br(std::move(fbr)) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g);
        };

        class ChainStatement : public Statement {
            Statement* lhs;
            Statement* rhs;
        public:
            ChainStatement(Statement* l, Statement* r)
                : lhs(l), rhs(r) {}
            void Build(llvm::IRBuilder<>& bb, Generator& g) {
                lhs->Build(bb, g);
                rhs->Build(bb, g);
            }
        };
        class WhileStatement : public Statement {
            Expression* cond;
            Statement* body;
        public:
            WhileStatement(Expression* c, Statement* b)
                : cond(c), body(b) {}
            void Build(llvm::IRBuilder<>& b, Generator& g);
        };
    }
}