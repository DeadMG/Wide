#pragma once

#include "AST.h"

#include "../../Util/MemoryArena.h"
#include "../../Util/ConcurrentQueue.h"

namespace Wide {
    namespace AST {
        class Builder {
            Concurrency::Queue<Wide::Memory::Arena> arenas;
            Module* GlobalModule;
        public:            
            Builder();

            IdentifierExpr* CreateIdentExpression(std::string name, Lexer::Range r);
            StringExpr* CreateStringExpression(std::string val, Lexer::Range r);
            MemAccessExpr* CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r);
            LeftShiftExpr* CreateLeftShiftExpression(Expression* lhs, Expression* rhs);
            FunctionCallExpr* CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            Return* CreateReturn(Expression* expr, Lexer::Range r);
            Return* CreateReturn(Lexer::Range r);
            VariableStatement* CreateVariableStatement(std::string name, Expression* value, Lexer::Range r);
            VariableStatement* CreateVariableStatement(std::string name, Lexer::Range r) { return CreateVariableStatement(std::move(name), nullptr, r); }
            AssignmentExpr* CreateAssignmentExpression(Expression* lhs, Expression* rhs);
            IntegerExpression* CreateIntegerExpression(std::string val, Lexer::Range r);
            RightShiftExpr* CreateRightShiftExpression(Expression* lhs, Expression* rhs);
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc);
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc);
            CompoundStatement* CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc);
            EqCmpExpression* CreateEqCmpExpression(Expression* lhs, Expression* rhs);
            NotEqCmpExpression* CreateNotEqCmpExpression(Expression* lhs, Expression* rhs);
            MetaCallExpr* CreateMetaFunctionCallExpression(Expression*, std::vector<Expression*>, Lexer::Range r);
            WhileStatement* CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc);
            ThisExpression* CreateThisExpression(Lexer::Range loc);
            Lambda* CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> caps);
            NegateExpression* CreateNegateExpression(Expression* e, Lexer::Range loc);
            DereferenceExpression* CreateDereferenceExpression(Expression* e, Lexer::Range loc);
            PointerAccess* CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r);

            OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs);
            XorExpression* CreateXorExpression(Expression* lhs, Expression* rhs);
            AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs);
            LTExpression* CreateLTExpression(Expression* lhs, Expression* rhs);
            LTEExpression* CreateLTEExpression(Expression* lhs, Expression* rhs);
            GTExpression* CreateGTExpression(Expression* lhs, Expression* rhs);
            GTEExpression* CreateGTEExpression(Expression* lhs, Expression* rhs);
            Increment* CreatePrefixIncrement(Expression* ex, Lexer::Range r);
            Increment* CreatePostfixIncrement(Expression* ex, Lexer::Range r);
            Addition* CreateAdditionExpression(Expression* lhs, Expression* rhs);
            Multiply* CreateMultiplyExpression(Expression* lhs, Expression* rhs);
            AutoExpression* CreateAutoExpression(Lexer::Range loc);
            Decrement* CreatePrefixDecrement(Expression* ex, Lexer::Range r);
            Decrement* CreatePostfixDecrement(Expression* ex, Lexer::Range r);
            AddressOfExpression* CreateAddressOf(Expression* ex, Lexer::Range r);

            std::vector<Statement*> CreateStatementGroup();
            std::vector<Expression*> CreateExpressionGroup();
            std::vector<VariableStatement*> CreateCaptureGroup();
            std::vector<VariableStatement*> CreateInitializerGroup() { return CreateCaptureGroup(); }
            
            Module* CreateModule(std::string val, Module* p);
            Using* CreateUsingDefinition(std::string val, Expression* expr, Module* p);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Type* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            Type* CreateType(std::string name, DeclContext* p, Lexer::Range loc);
            Type* CreateType(std::string name, Lexer::Range loc) { return CreateType(std::move(name), nullptr, loc); }
            void SetTypeEndLocation(Lexer::Range loc, Type* t);
            
            std::vector<FunctionArgument> CreateFunctionArgumentGroup();

            void AddTypeField(Type*, VariableStatement*);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>&, std::string, Expression*);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>&, std::string);
            void AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement*);
            void AddInitializerToGroup(std::vector<VariableStatement*>& l, VariableStatement* b) { return AddCaptureToGroup(l, b); }

            void AddStatementToGroup(std::vector<Statement*>&, Statement*);
            void AddExpressionToGroup(std::vector<Expression*>&, Expression*);

            Lexer::Range GetLocation(Statement* s) {
                return s->location;
            }

            Module* GetGlobalModule();

            typedef Statement* StatementType;
            typedef Expression* ExpressionType;
        };
    }
}