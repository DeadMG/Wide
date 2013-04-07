#pragma once

#include "AST.h"

#include "../../Util/MemoryArena.h"

namespace Wide {
    namespace AST {
        class Builder {
            Wide::Memory::Arena arena;
        public:            
            Builder();

            typedef ModuleLevelDeclaration* ModuleLevelDeclaration;

            std::vector<Statement*> CreateStatementGroup();
            std::vector<Expression*> CreateExpressionGroup();
            IdentifierExpr* CreateIdentExpression(std::string name, Lexer::Range r);
            StringExpr* CreateStringExpression(std::string val, Lexer::Range r);
            MemAccessExpr* CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r);
            LeftShiftExpr* CreateLeftShiftExpression(Expression* lhs, Expression* rhs);
            FunctionCallExpr* CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            Return* CreateReturn(Expression* expr, Lexer::Range r);
            Return* CreateReturn(Lexer::Range r);
            VariableStatement* CreateVariableStatement(std::string name, Expression* value, Lexer::Range r);
            AssignmentExpr* CreateAssignmentExpression(Expression* lhs, Expression* rhs);
            IntegerExpression* CreateIntegerExpression(std::string val, Lexer::Range r);
            RightShiftExpr* CreateRightShiftExpression(Expression* lhs, Expression* rhs);
            QualifiedName* CreateQualifiedName();
            std::vector<Function::FunctionArgument> CreateFunctionArgumentGroup();
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br);
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br);
            CompoundStatement* CreateCompoundStatement(std::vector<Statement*> true_br);
            EqCmpExpression* CreateEqCmpExpression(Expression* lhs, Expression* rhs);
            NotEqCmpExpression* CreateNotEqCmpExpression(Expression* lhs, Expression* rhs);
            MetaCallExpr* CreateMetaFunctionCallExpression(Expression*, std::vector<Expression*>, Lexer::Range r);
            WhileStatement* CreateWhileStatement(Expression* cond, Statement* body);

            OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs);
            XorExpression* CreateXorExpression(Expression* lhs, Expression* rhs);
            AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs);
            LTExpression* CreateLTExpression(Expression* lhs, Expression* rhs);
            LTEExpression* CreateLTEExpression(Expression* lhs, Expression* rhs);
            GTExpression* CreateGTExpression(Expression* lhs, Expression* rhs);
            GTEExpression* CreateGTEExpression(Expression* lhs, Expression* rhs);

            Module* CreateModule(std::string val, Module* p);
            Using* CreateUsingDefinition(std::string val, Expression* expr, Module* p);
            Function* CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* p, std::vector<Function::FunctionArgument>);

            void AddNameToQualifiedName(QualifiedName* name, std::string val);
            void AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>&, std::string, Expression*);
            void AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>&, std::string);

            Module* const GlobalModule;
        };
    }
}