#pragma once

#include "Statement.h"

#define _SCL_SECURE_NO_WARNINGS

#include <vector>
#include <functional>

namespace llvm {
    class Type;
};
namespace clang {
    class VarDecl;
}
namespace Wide {
    namespace ClangUtil {
        class ClangTU;
    }
    namespace Semantic {
        struct Type;
        struct Variable;
    }
    namespace Codegen { 
        struct Expression : Statement {
        public:
            Expression() : val(nullptr) {}

            void Build(llvm::IRBuilder<>& bb);
            llvm::Value* GetValue(llvm::IRBuilder<>& bb);
        protected:
            llvm::Value* val;
            virtual llvm::Value* ComputeValue(llvm::IRBuilder<>&) = 0;
        };

        class Variable : public Expression {
            std::function<llvm::Type*(llvm::Module*)> t;
        public:
            Variable(std::function<llvm::Type*(llvm::Module*)> ty)
                : t(std::move(ty)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>&);
        };

        class FunctionCall : public Expression {
            std::vector<Expression*> arguments;
            Expression* object;
            std::function<llvm::Type*(llvm::Module*)> CastTy;
        public:
            Expression* GetCallee() { return object; }
            FunctionCall(Expression* obj, std::vector<Expression*> args, std::function<llvm::Type*(llvm::Module*)>);
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };
        
        class FunctionValue : public Expression {
            std::string mangled_name;
        public:
            std::string GetMangledName() {
                return mangled_name;
            }
            FunctionValue(std::string name);
            llvm::Value* ComputeValue(llvm::IRBuilder<>&);
        };

        class LoadExpression : public Expression {
            Expression* obj;
        public:
            LoadExpression(Expression* o)
                : obj(o) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };
        
        class ChainExpression : public Expression {
            Statement* s;
            Expression* next;
        public:
            ChainExpression(Statement* stat, Expression* e)
                : s(stat), next(e) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };
        
        class StringExpression : public Expression {
            std::string value;
        public:
            StringExpression(std::string expr)
                : value(std::move(expr)) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);

            
            std::string GetContents() {
                return value;
            }
        };

        class NamedGlobalVariable : public Expression {
            std::string mangled;
        public:
            NamedGlobalVariable(std::string mangledname)
                : mangled(std::move(mangledname)) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>&);
        };              

        class StoreExpression : public Expression {
            Expression* obj;
            Expression* val;
        public:
            StoreExpression(Expression* l, Expression* r)
                : obj(l), val(r) {}
            
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class Int8Expression : public Expression {
            char value;
        public:
            Int8Expression(char val)
                : value(val) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class FieldExpression : public Expression {
            unsigned fieldnum;
            Expression* obj;
        public:
            FieldExpression(unsigned f, Expression* o)
                : fieldnum(f), obj(o) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class ParamExpression : public Expression {
            std::function<unsigned()> param;
        public:
            ParamExpression(std::function<unsigned()>  p)
                : param(std::move(p)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class TruncateExpression : public Expression {
            Expression* val;
            std::function<llvm::Type*(llvm::Module*)> ty;
        public:
            TruncateExpression(Expression* e, std::function<llvm::Type*(llvm::Module*)> type)
                : val(e), ty(std::move(type)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class NullExpression : public Expression {
            std::function<llvm::Type*(llvm::Module*)> ty;
        public:
            NullExpression(std::function<llvm::Type*(llvm::Module*)> type)
                : ty(std::move(type)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class IntegralLeftShiftExpression : public Expression {
            Expression* lhs;
            Expression* rhs;
        public:
            IntegralLeftShiftExpression(Expression* l, Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };

        class IntegralRightShiftExpression : public Expression {
            Expression* lhs;
            Expression* rhs;
        public:
            IntegralRightShiftExpression(Expression* l, Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder);
        };
    }
}