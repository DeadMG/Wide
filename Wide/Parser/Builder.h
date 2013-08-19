#pragma once

#include <functional>
#include <Wide/Parser/AST.h>
#include <Wide/Util/MemoryArena.h>
#include <Wide/Util/ConcurrentQueue.h>

namespace Wide {
    namespace Parser {
        enum Error : int;
    }
    namespace AST {
        class Builder;
        class ThreadLocalBuilder {
            Wide::Memory::Arena arena;
            Builder* b;
            std::function<void(Lexer::Range, Parser::Error)> error;
        public:
            ThreadLocalBuilder(Builder&, std::function<void(Lexer::Range, Parser::Error)>);
            ~ThreadLocalBuilder();

            IdentifierExpr* CreateIdentExpression(std::string name, Lexer::Range r);
            StringExpr* CreateStringExpression(std::string val, Lexer::Range r);
            MemAccessExpr* CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r);
            BinaryExpression* CreateLeftShiftExpression(Expression* lhs, Expression* rhs);
            FunctionCallExpr* CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            Return* CreateReturn(Expression* expr, Lexer::Range r);
            Return* CreateReturn(Lexer::Range r);
            VariableStatement* CreateVariableStatement(std::string name, Expression* value, Lexer::Range r);
            VariableStatement* CreateVariableStatement(std::string name, Lexer::Range r);
			BinaryExpression* CreateAssignmentExpression(Expression* lhs, Expression* rhs, Lexer::TokenType type);
            IntegerExpression* CreateIntegerExpression(std::string val, Lexer::Range r);
            BinaryExpression* CreateRightShiftExpression(Expression* lhs, Expression* rhs);
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc);
            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc);
            CompoundStatement* CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc);
            BinaryExpression* CreateEqCmpExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateNotEqCmpExpression(Expression* lhs, Expression* rhs);
            MetaCallExpr* CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            WhileStatement* CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc);
            ThisExpression* CreateThisExpression(Lexer::Range loc);
            Lambda* CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> caps);
            NegateExpression* CreateNegateExpression(Expression* e, Lexer::Range loc);
            DereferenceExpression* CreateDereferenceExpression(Expression* e, Lexer::Range loc);
            PointerAccess* CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r);
            BinaryExpression* CreateOrExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateXorExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateAndExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateLTExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateLTEExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateGTExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateGTEExpression(Expression* lhs, Expression* rhs);
            Increment* CreatePrefixIncrement(Expression* ex, Lexer::Range r);
            Increment* CreatePostfixIncrement(Expression* ex, Lexer::Range r);
            BinaryExpression* CreateAdditionExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateSubtractionExpression(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateMultiplyExpression(Expression* lhs, Expression* rhs);
			BinaryExpression* CreateModulusExpression(Expression* lhs, Expression* rhs);
			BinaryExpression* CreateDivisionExpression(Expression* lhs, Expression* rhs);
            AutoExpression* CreateAutoExpression(Lexer::Range loc);
            Decrement* CreatePrefixDecrement(Expression* ex, Lexer::Range r);
            Decrement* CreatePostfixDecrement(Expression* ex, Lexer::Range r);
            AddressOfExpression* CreateAddressOf(Expression* ex, Lexer::Range r);
            std::vector<Statement*> CreateStatementGroup();
            std::vector<Expression*> CreateExpressionGroup();
            std::vector<VariableStatement*> CreateCaptureGroup();
            std::vector<VariableStatement*> CreateInitializerGroup();            
            Module* CreateModule(std::string val, Module* p, Lexer::Range r);
            Using* CreateUsingDefinition(std::string val, Expression* expr, Module* p);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Type* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            Type* CreateType(std::string name, DeclContext* p, Lexer::Range loc);
            Type* CreateType(std::string name, Lexer::Range loc);
            void SetTypeEndLocation(Lexer::Range loc, Type* t);
			void SetModuleEndLocation(Module* m, Lexer::Range loc);            
            std::vector<FunctionArgument> CreateFunctionArgumentGroup();
            void AddTypeField(Type* t, VariableStatement* decl);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Expression* expr);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name);
            void AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement* cap);
            void AddInitializerToGroup(std::vector<VariableStatement*>& l, VariableStatement* b);
            void AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt);
            void AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr);
            Lexer::Range GetLocation(Statement* s);
            void Error(Wide::Lexer::Range, Parser::Error);

            typedef Statement* StatementType;
            typedef Expression* ExpressionType;
        };
        class Builder {
            Concurrency::Queue<Wide::Memory::Arena> arenas;
            Module* GlobalModule;
            friend class ThreadLocalBuilder;
        public:            
            Builder();

            Module* GetGlobalModule();
        };
    }
}