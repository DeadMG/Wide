#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "../Lexer/Token.h"

#include "../../Util/ConcurrentUnorderedMap.h"
#include "../../Util/ConcurrentVector.h"

namespace Wide {
    namespace AST {
        struct Statement { 
            Statement(Lexer::Range r)
                : location(r) {}
            virtual ~Statement() {}
            Lexer::Range location;
        };
        struct Module;
        struct Expression : Statement {
            Expression(Lexer::Range r)
                : Statement(r) {}
        };
        struct ThisExpression : Expression {
            ThisExpression(Lexer::Range r)
                : Expression(r) {}
        };
        struct DeclContext;
        struct ModuleLevelDeclaration {
            ModuleLevelDeclaration(DeclContext* above, std::string nam)
                : higher(above), name(std::move(nam)) {}
            DeclContext* higher;
            virtual ~ModuleLevelDeclaration() {} 
            std::string GetName() { return name; }
            std::string name;
        };
        struct DeclContext : public ModuleLevelDeclaration {
            DeclContext(DeclContext* nested, std::string name)
                : ModuleLevelDeclaration(nested, std::move(name)) {}
        };
        struct Module : public DeclContext {
            Module(std::string nam, Module* parent)
                : DeclContext(parent, std::move(nam)) {}
            Concurrency::UnorderedMap<std::string, ModuleLevelDeclaration*> decls;
        };
        struct FunctionOverloadSet;
        struct VariableStatement;
        struct Type : public DeclContext, Expression {
            Type(DeclContext* above, std::string name) : DeclContext(above, name), Expression(Lexer::Range()) {}
            std::vector<VariableStatement*> variables;
            std::unordered_map<std::string, FunctionOverloadSet*> Functions;
        };
        struct IdentifierExpr : Expression {
            IdentifierExpr(std::string nam, Lexer::Range loc)
                : val(std::move(nam)), Expression(loc) {}
            std::string val;
        };
        struct StringExpr : Expression {
            StringExpr(std::string str, Lexer::Range r)
                : val(std::move(str)), Expression(r) {}
            std::string val;
        };
        struct MemAccessExpr : Expression {
            MemAccessExpr(std::string nam, Expression* e, Lexer::Range r)
                : mem(std::move(nam)), expr(e), Expression(r) {}
            std::string mem;
            Expression* expr;
        };
        struct BinaryExpression : public Expression {
            BinaryExpression(Expression* l, Expression* r)
                : lhs(l), rhs(r), Expression(l->location + r->location) {}
            Expression* lhs;
            Expression* rhs;
        };
        struct LeftShiftExpr : BinaryExpression {
            LeftShiftExpr(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct RightShiftExpr : BinaryExpression {
            RightShiftExpr(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct AssignmentExpr : BinaryExpression {
            AssignmentExpr(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct FunctionArgument {
             // May be null
            Expression* type;
            std::string name;
        };
        struct Lambda : Expression {
            std::vector<FunctionArgument> args;
            std::vector<Statement*> statements;
            std::vector<VariableStatement*> Captures;
            bool defaultref;
            Lambda(std::vector<Statement*> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<VariableStatement*> caps)
                : args(std::move(arg)), statements(std::move(body)), Expression(r), defaultref(ref), Captures(std::move(caps)) {}
        };
        struct Function : DeclContext {
            Function(std::string nam, std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, DeclContext* m, std::vector<VariableStatement*> caps)
                : prolog(std::move(prolog)), statements(std::move(b)), location(loc), args(std::move(ar)), DeclContext(m, std::move(nam)), initializers(std::move(caps)) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> prolog;
            std::vector<Statement*> statements;
            std::vector<VariableStatement*> initializers;
            Lexer::Range location;
        };
        struct FunctionCallExpr : Expression {
            FunctionCallExpr(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                : callee(obj), args(std::move(arg)), Expression(loc) {}
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct FunctionOverloadSet : public ModuleLevelDeclaration {
            FunctionOverloadSet(std::string nam, DeclContext* p)
                : ModuleLevelDeclaration(p, std::move(nam)) {}
            Concurrency::Vector<Function*> functions;
        };
        /*struct QualifiedName {
            std::vector<std::string> components;
        };*/
        struct Using : public ModuleLevelDeclaration {
            Using(std::string name, Expression* ex, Module* m)
                : expr(ex), ModuleLevelDeclaration(m, std::move(name)) {}
            Expression* expr;
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : RetExpr(nullptr), Statement(r) {}
            Return(Expression* e, Lexer::Range r) : RetExpr(e), Statement(r) {}
            Expression* RetExpr;
        };
        struct VariableStatement : public Statement {
            VariableStatement(std::string nam, Expression* expr, Lexer::Range r)
                : name(std::move(nam)), initializer(expr), Statement(r) {}
            std::string name;
            Expression* initializer;
        };
        struct IntegerExpression : public Expression {
            IntegerExpression(std::string val, Lexer::Range loc)
                : integral_value(std::move(val)), Expression(loc) {}
            std::string integral_value;
        };
        struct CompoundStatement : public Statement {
            CompoundStatement(std::vector<Statement*> body, Lexer::Range loc)
                : stmts(std::move(body)), Statement(loc) {}
            std::vector<Statement*> stmts;
        };
        struct IfStatement : public Statement {
            IfStatement(Expression* c, Statement* t, Statement* f, Lexer::Range loc)
                : true_statement(t), false_statement(f), condition(c), Statement(loc) {}
            Statement* true_statement;
            Statement* false_statement;
            Expression* condition;
        };
        struct EqCmpExpression : public BinaryExpression {
            EqCmpExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct NotEqCmpExpression : public BinaryExpression {
            NotEqCmpExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct AutoExpression : public Expression {
            AutoExpression(Lexer::Range loc)
                : Expression(loc) {}
        };
        struct UnaryExpression : public Expression {
            UnaryExpression(Expression* expr, Lexer::Range loc)
                : ex(expr), Expression(loc) {}
            Expression* ex;
        };
        struct PointerAccess : public UnaryExpression {
            std::string member;
            PointerAccess(std::string name, Expression* expr, Lexer::Range loc)
                : UnaryExpression(expr, loc), member(std::move(name)) {}
        };
        struct DereferenceExpression : public UnaryExpression {
            DereferenceExpression(Expression* e, Lexer::Range pos)
                : UnaryExpression(e, pos) {}
        };
        struct NegateExpression : public UnaryExpression {
            NegateExpression(Expression* e, Lexer::Range pos)
                : UnaryExpression(e, pos) {}
        };
        struct OrExpression : public BinaryExpression {
            OrExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct XorExpression : public BinaryExpression {
            XorExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct AndExpression : public BinaryExpression {
            AndExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct LTExpression : public BinaryExpression {
            LTExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct LTEExpression : public BinaryExpression {
            LTEExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct GTExpression : public BinaryExpression {
            GTExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct GTEExpression : public BinaryExpression {
            GTEExpression(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };

        struct MetaCallExpr : public Expression {        
            MetaCallExpr(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                : callee(obj), args(std::move(arg)), Expression(loc) {}    
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct WhileStatement : public Statement {
            WhileStatement(Statement* b, Expression* c, Lexer::Range loc)
                : body(b), condition(c), Statement(loc) {}
            Statement* body;
            Expression* condition;
        };
        struct Increment : public UnaryExpression {
            bool postfix;
            Increment(Expression* ex, Lexer::Range r, bool post)
                : UnaryExpression(ex, r), postfix(post) {}
        };
        struct Decrement : public UnaryExpression {
            bool postfix;
            Decrement(Expression* ex, Lexer::Range r, bool post)
                : UnaryExpression(ex, r), postfix(post) {}
        };
        struct Addition : public BinaryExpression {
            Addition(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
        struct Multiply : public BinaryExpression {
            Multiply(Expression* l, Expression* r) : BinaryExpression(l, r) {}
        };
    }
}