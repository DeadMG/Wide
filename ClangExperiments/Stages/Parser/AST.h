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
        struct ModuleLevelDeclaration {
            ModuleLevelDeclaration(Module* above)
                : higher(above) {}
            Module* higher;
            virtual ~ModuleLevelDeclaration() {} 
            virtual std::string GetName() = 0; 
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
        struct Function : ModuleLevelDeclaration {
            std::string name;
            struct FunctionArgument {
                 // May be null
                Expression* type;
                std::string name;
            };
            Function(std::string nam, std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, Module* m)
                : name(std::move(nam)), prolog(std::move(prolog)), statements(std::move(b)), location(loc), args(std::move(ar)), ModuleLevelDeclaration(m) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> prolog;
            std::vector<Statement*> statements;
            std::string GetName() {
                return name;
            }
            Lexer::Range location;
        };
        struct FunctionCallExpr : Expression {
            FunctionCallExpr(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                : callee(obj), args(std::move(arg)), Expression(loc) {}
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct Module : public ModuleLevelDeclaration {
            std::string name;
            Module(std::string nam, Module* parent)
                : name(std::move(nam)), ModuleLevelDeclaration(parent) {}
            std::string GetName() { return name; }
            Concurrency::UnorderedMap<std::string, ModuleLevelDeclaration*> decls;
        };
        struct FunctionOverloadSet : public ModuleLevelDeclaration {
            FunctionOverloadSet(std::string nam, Module* p)
                : name(std::move(nam)), ModuleLevelDeclaration(p) {}
            virtual std::string GetName() { return name; }
            std::string name;
            Concurrency::Vector<Function*> functions;
        };
        /*struct QualifiedName {
            std::vector<std::string> components;
        };*/
        struct Using : public ModuleLevelDeclaration {
            Using(std::string name, Expression* ex, Module* m)
                : identifier(std::move(name)), expr(ex), ModuleLevelDeclaration(m) {}
            std::string GetName() {
                return identifier;
            }
            std::string identifier;
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
    }
}