#pragma once

#include "AST.h"

#include "../../Util/MemoryArena.h"
#include "../../Util/ConcurrentQueue.h"

namespace Wide {
    namespace AST {
        class Builder;
        class ThreadLocalBuilder {
            Wide::Memory::Arena arena;
            Builder* b;
        public:
            ThreadLocalBuilder(Builder&);
            ~ThreadLocalBuilder();

            IdentifierExpr* CreateIdentExpression(std::string name, Lexer::Range r) 
            { return arena.Allocate<IdentifierExpr>(std::move(name), r); }

            StringExpr* CreateStringExpression(std::string val, Lexer::Range r) 
            { return arena.Allocate<StringExpr>(std::move(val), r); }

            MemAccessExpr* CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
            { return arena.Allocate<MemAccessExpr>(std::move(mem), e, r); }

            LeftShiftExpr* CreateLeftShiftExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<LeftShiftExpr>(lhs, rhs); }

            FunctionCallExpr* CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
            { return arena.Allocate<FunctionCallExpr>(e, std::move(args), r); }

            Return* CreateReturn(Expression* expr, Lexer::Range r) 
            { return arena.Allocate<Return>(expr, r); }

            Return* CreateReturn(Lexer::Range r) 
            { return arena.Allocate<Return>(r); }

            VariableStatement* CreateVariableStatement(std::string name, Expression* value, Lexer::Range r) 
            { return arena.Allocate<VariableStatement>(std::move(name), value, r); }

            VariableStatement* CreateVariableStatement(std::string name, Lexer::Range r) 
            { return CreateVariableStatement(std::move(name), nullptr, r); }

            AssignmentExpr* CreateAssignmentExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<AssignmentExpr>(lhs, rhs); }

            IntegerExpression* CreateIntegerExpression(std::string val, Lexer::Range r) 
            { return arena.Allocate<IntegerExpression>(std::move(val), r); }

            RightShiftExpr* CreateRightShiftExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<RightShiftExpr>(lhs, rhs); }

            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc) 
            { return arena.Allocate<IfStatement>(cond, true_br, false_br, loc); }

            IfStatement* CreateIfStatement(Expression* cond, Statement* true_br, Lexer::Range loc) 
            { return CreateIfStatement(cond, true_br, nullptr, loc); }

            CompoundStatement* CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc) 
            { return arena.Allocate<CompoundStatement>(std::move(true_br), loc); }

            EqCmpExpression* CreateEqCmpExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<EqCmpExpression>(lhs, rhs); }

            NotEqCmpExpression* CreateNotEqCmpExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<NotEqCmpExpression>(lhs, rhs); }

            MetaCallExpr* CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
            { return arena.Allocate<MetaCallExpr>(e, std::move(args), r); }

            WhileStatement* CreateWhileStatement(Expression* cond, Statement* body, Lexer::Range loc) 
            { return arena.Allocate<WhileStatement>(body, cond, loc); }

            ThisExpression* CreateThisExpression(Lexer::Range loc) 
            { return arena.Allocate<ThisExpression>(loc); }

            Lambda* CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<VariableStatement*> caps) 
            { return arena.Allocate<Lambda>(std::move(body), std::move(args), loc, defaultref, std::move(caps)); }

            NegateExpression* CreateNegateExpression(Expression* e, Lexer::Range loc) 
            { return arena.Allocate<NegateExpression>(e, loc); }

            DereferenceExpression* CreateDereferenceExpression(Expression* e, Lexer::Range loc) 
            { return arena.Allocate<DereferenceExpression>(e, loc); }

            PointerAccess* CreatePointerAccessExpression(std::string mem, Expression* e, Lexer::Range r) 
            { return arena.Allocate<PointerAccess>(std::move(mem), e, r); }

            OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<OrExpression>(lhs, rhs); }

            XorExpression* CreateXorExpression(Expression* lhs, Expression* rhs)
            { return arena.Allocate<XorExpression>(lhs, rhs); }

            AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs)
            { return arena.Allocate<AndExpression>(lhs, rhs); }

            LTExpression* CreateLTExpression(Expression* lhs, Expression* rhs)
            { return arena.Allocate<LTExpression>(lhs, rhs); }

            LTEExpression* CreateLTEExpression(Expression* lhs, Expression* rhs)
            { return arena.Allocate<LTEExpression>(lhs, rhs); }

            GTExpression* CreateGTExpression(Expression* lhs, Expression* rhs)
            { return arena.Allocate<GTExpression>(lhs, rhs); }

            GTEExpression* CreateGTEExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<GTEExpression>(lhs, rhs); }

            Increment* CreatePrefixIncrement(Expression* ex, Lexer::Range r)
            { return arena.Allocate<Increment>(ex, r, false); }

            Increment* CreatePostfixIncrement(Expression* ex, Lexer::Range r)
            { return arena.Allocate<Increment>(ex, r, true); }

            Addition* CreateAdditionExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<Addition>(lhs, rhs); }

            Multiply* CreateMultiplyExpression(Expression* lhs, Expression* rhs) 
            { return arena.Allocate<Multiply>(lhs, rhs); }

            AutoExpression* CreateAutoExpression(Lexer::Range loc)
            { return arena.Allocate<AutoExpression>(loc); }

            Decrement* CreatePrefixDecrement(Expression* ex, Lexer::Range r) 
            { return arena.Allocate<Decrement>(ex, r, false); }

            Decrement* CreatePostfixDecrement(Expression* ex, Lexer::Range r) 
            { return arena.Allocate<Decrement>(ex, r, true); }

            AddressOfExpression* CreateAddressOf(Expression* ex, Lexer::Range r) 
            { return arena.Allocate<AddressOfExpression>(ex, r); }

            std::vector<Statement*> CreateStatementGroup() 
            { return std::vector<Statement*>(); }

            std::vector<Expression*> CreateExpressionGroup() 
            { return std::vector<Expression*>(); }

            std::vector<VariableStatement*> CreateCaptureGroup() 
            { return std::vector<VariableStatement*>(); }

            std::vector<VariableStatement*> CreateInitializerGroup() 
            { return CreateCaptureGroup(); }
            
            Module* CreateModule(std::string val, Module* p);
            Using* CreateUsingDefinition(std::string val, Expression* expr, Module* p);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Type* p, std::vector<FunctionArgument>, std::vector<VariableStatement*> caps);
            Type* CreateType(std::string name, DeclContext* p, Lexer::Range loc);
            Type* CreateType(std::string name, Lexer::Range loc) { return CreateType(std::move(name), nullptr, loc); }
            void SetTypeEndLocation(Lexer::Range loc, Type* t) { t->location = t->location + loc; }
            
            std::vector<FunctionArgument> CreateFunctionArgumentGroup() { return std::vector<FunctionArgument>(); }

            void AddTypeField(Type* t, VariableStatement* decl) { t->variables.push_back(decl); }
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Expression* expr)  {
                FunctionArgument arg;
                arg.name = name;
                arg.type = expr;
                args.push_back(arg);
            }
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name) { return AddArgumentToFunctionGroup(args, std::move(name), nullptr); }
            void AddCaptureToGroup(std::vector<VariableStatement*>& l, VariableStatement* cap) { return l.push_back(cap); }
            void AddInitializerToGroup(std::vector<VariableStatement*>& l, VariableStatement* b) { return AddCaptureToGroup(l, b); }
            void AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt) { return grp.push_back(stmt); }
            void AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr) { return grp.push_back(expr); }

            Lexer::Range GetLocation(Statement* s) {
                return s->location;
            }

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