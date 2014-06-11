#pragma once

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <Wide/Parser/AST.h>
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Util/Concurrency/ConcurrentQueue.h>

namespace Wide {
    namespace Parser {
        enum class Error : int;
        enum class Warning : int;
    }
    namespace AST {
        enum class OutliningType : int {
            Module,
            Function,
            Type
        };
        class Builder {
            Wide::Memory::Arena arena;
            Module GlobalModule;
            std::function<void(std::vector<Wide::Lexer::Range>, Parser::Error)> error;
            std::function<void(Lexer::Range, Parser::Warning)> warning;
            std::function<void(Lexer::Range, OutliningType)> outlining;
            std::unordered_map<Module*, std::unordered_map<std::string, std::unordered_set<DeclContext*>>> combine_errors;
        public:
            Builder(
                std::function<void(std::vector<Wide::Lexer::Range>, Parser::Error)>, 
                std::function<void(Lexer::Range, Parser::Warning)>, 
                std::function<void(Lexer::Range, OutliningType)>
            );

            Tuple* CreateTuple(std::vector<Expression*> expressions, Lexer::Range r);
            Identifier* CreateIdentifier(std::string name, Lexer::Range r);
            String* CreateString(std::string val, Lexer::Range r);
            MemberAccess* CreateMemberAccess(std::string mem, Expression* e, Lexer::Range r, Lexer::Range);
            BinaryExpression* CreateLeftShift(Expression* lhs, Expression* rhs);
            FunctionCall* CreateFunctionCall(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            Return* CreateReturn(Expression* expr, Lexer::Range r);
            Return* CreateReturn(Lexer::Range r);
            Variable* CreateVariable(std::vector<Variable::Name> name, Expression* value, Lexer::Range r);
            Variable* CreateVariable(std::string name, Expression* value, Lexer::Range r, Lexer::Range nameloc);
            Variable* CreateVariable(std::string name, Lexer::Range r, Lexer::Range nameloc);
            BinaryExpression* CreateAssignment(Expression* lhs, Expression* rhs, Lexer::TokenType type);
            Integer* CreateInteger(std::string val, Lexer::Range r);
            BinaryExpression* CreateRightShift(Expression* lhs, Expression* rhs);
            If* CreateIf(Variable* cond, Statement* true_br, Statement* false_br, Lexer::Range loc);
            If* CreateIf(Variable* cond, Statement* true_br, Lexer::Range loc);
            If* CreateIf(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc);
            If* CreateIf(Expression* cond, Statement* true_br, Lexer::Range loc);
            CompoundStatement* CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc);
            BinaryExpression* CreateEqCmp(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateNotEqCmp(Expression* lhs, Expression* rhs);
            MetaCall* CreateMetaFunctionCall(Expression* e, std::vector<Expression*> args, Lexer::Range r);
            While* CreateWhile(Expression* cond, Statement* body, Lexer::Range loc);
            While* CreateWhile(Variable* cond, Statement* body, Lexer::Range loc);
            This* CreateThis(Lexer::Range loc);
            True* CreateTrue(Lexer::Range loc);
            False* CreateFalse(Lexer::Range loc);
            Lambda* CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<Variable*> caps);
            Negate* CreateNegate(Expression* e, Lexer::Range loc);
            Dereference* CreateDereference(Expression* e, Lexer::Range loc);
            PointerMemberAccess* CreatePointerAccess(std::string mem, Expression* e, Lexer::Range r, Lexer::Range);
            BinaryExpression* CreateOr(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateXor(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateAnd(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateLTE(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateLT(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateGTE(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateGT(Expression* lhs, Expression* rhs);
            Increment* CreatePrefixIncrement(Expression* ex, Lexer::Range r);
            Increment* CreatePostfixIncrement(Expression* ex, Lexer::Range r);
            BinaryExpression* CreateAddition(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateSubtraction(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateMultiply(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateModulus(Expression* lhs, Expression* rhs);
            BinaryExpression* CreateDivision(Expression* lhs, Expression* rhs);
            Decltype* CreateDecltype(Expression* arg, Lexer::Range loc);
            Typeid* CreateTypeid(Expression* arg, Lexer::Range loc);
            DynamicCast* CreateDynamicCast(Expression* type, Expression* object, Lexer::Range loc);
            Auto* CreateAuto(Lexer::Range loc);
            ErrorExpr* CreateError(Lexer::Range loc);
            Decrement* CreatePrefixDecrement(Expression* ex, Lexer::Range r);
            Decrement* CreatePostfixDecrement(Expression* ex, Lexer::Range r);
            Throw* CreateThrow(Lexer::Range);
            Throw* CreateThrow(Lexer::Range, Expression*);
            Break* CreateBreak(Lexer::Range r);
            Continue* CreateContinue(Lexer::Range r);
            AddressOf* CreateAddressOf(Expression* ex, Lexer::Range r);
            std::vector<Statement*> CreateStatementGroup();
            std::vector<Expression*> CreateExpressionGroup();
            std::vector<Variable*> CreateCaptureGroup();
            std::vector<Variable*> CreateInitializerGroup();            
            Module* CreateModule(std::string val, Module* p, Lexer::Range decl, Lexer::Access a);
            Using* CreateUsing(std::string val, Lexer::Range loc, Expression* expr, Module* p, Lexer::Access a);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range where, Lexer::Range r, Module* p, std::vector<FunctionArgument>, std::vector<Variable*> caps, Lexer::Access a, bool dynamic);
            void CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range where, Lexer::Range r, Type* p, std::vector<FunctionArgument>, std::vector<Variable*> caps, Lexer::Access a, bool dynamic);
            void CreateOverloadedOperator(Wide::Lexer::TokenType name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Module* p, std::vector<FunctionArgument>, Lexer::Access a);
            void CreateOverloadedOperator(Wide::Lexer::TokenType name, std::vector<Statement*> body, std::vector<Statement*> prolog, Lexer::Range r, Type* p, std::vector<FunctionArgument>, Lexer::Access a);
            Type* CreateType(std::vector<Expression*> bases, Lexer::Range loc, Lexer::Access a);
            void AddTypeToModule(Module* m, std::string name, Type* t);
            void AddTemplateTypeToModule(Module* m, std::string name, std::vector<FunctionArgument>, Type* t);
            void SetTypeEndLocation(Lexer::Range loc, Type* t);
            void SetModuleEndLocation(Module* m, Lexer::Range loc);            
            std::vector<FunctionArgument> CreateFunctionArgumentGroup();
            void AddTypeField(Type* t, Variable* decl, Lexer::Access a);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Lexer::Range r, Expression* expr);
            void AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Lexer::Range r);
            void AddCaptureToGroup(std::vector<Variable*>& l, Variable* cap);
            void AddInitializerToGroup(std::vector<Variable*>& l, Variable* b);
            void AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt);
            void AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr);
            Lexer::Range GetLocation(Statement* s);
            void OutlineFunction(Lexer::Range r);
            std::vector<Variable::Name> CreateVariableNameGroup();
            void AddNameToGroup(std::vector<Variable::Name>&, std::string, Lexer::Range where);

            void Error(std::vector<Wide::Lexer::Range>, Parser::Error);
            void Error(Wide::Lexer::Range, Parser::Error);
            void Warning(Wide::Lexer::Range, Parser::Warning);

            Module* GetGlobalModule() { return &GlobalModule; }

            std::vector<std::vector<std::pair<Lexer::Range, DeclContext*>>> GetCombinerErrors();

            typedef Statement* StatementType;
            typedef Expression* ExpressionType;
        };
    }
}