#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "../Lexer/Token.h"

namespace Wide {
    namespace AST {
        struct Statement { 
            virtual ~Statement() {}            
            Lexer::Range GetLocation() const {
                return location;
            }
            Lexer::Range location;
        };
        struct Module;
        struct Expression : Statement {};
        struct ModuleLevelDeclaration {
            ModuleLevelDeclaration()
                : higher(nullptr) {}
            Module* higher;
            virtual ~ModuleLevelDeclaration() {} 
            virtual std::string GetName() = 0; 
        };
        struct IdentifierExpr : Expression {
            std::string val;
        };
        struct StringExpr : Expression {
            std::string val;
        };
        struct MemAccessExpr : Expression {
            std::string mem;
            Expression* expr;
        };
        struct BinaryExpression : public Expression {
            Expression* lhs;
            Expression* rhs;
        };
        struct LeftShiftExpr : BinaryExpression {};
        struct RightShiftExpr : BinaryExpression {};
        struct AssignmentExpr : BinaryExpression {};
        struct Function : ModuleLevelDeclaration {
            std::string name;
            struct FunctionArgument {
                 // May be null
                Expression* type;
                std::string name;
            };
            std::vector<FunctionArgument> args;
            std::vector<Statement*> prolog;
            std::vector<Statement*> statements;
            std::string GetName() {
                return name;
            }
            Lexer::Range location;
        };
        struct FunctionCallExpr : Expression {
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct Module : public ModuleLevelDeclaration {
            std::string name;
            Module(std::string nam)
                : name(std::move(nam)) {}
            std::string GetName() { return name; }
            std::unordered_map<std::string, ModuleLevelDeclaration*> decls;
        };
        struct FunctionOverloadSet : public ModuleLevelDeclaration {
            virtual std::string GetName() { return name; }
            std::string name;
            std::vector<Function*> functions;
        };
        struct QualifiedName {
            std::vector<std::string> components;
        };
        struct Using : public ModuleLevelDeclaration {
            std::string GetName() {
                return identifier;
            }
            std::string identifier;
            Expression* expr;
        };
        struct Return : public Statement {
            Return() : RetExpr(nullptr) {}
            Expression* RetExpr;
        };
        struct VariableStatement : public Statement {
            std::string name;
            Expression* initializer;
        };
        struct IntegerExpression : public Expression {
            std::string integral_value;
        };
        struct CompoundStatement : public Statement {
            std::vector<Statement*> stmts;
        };
        struct IfStatement : public Statement {
            Statement* true_statement;
            Statement* false_statement;
            Expression* condition;
        };
        struct EqCmpExpression : public BinaryExpression {};
        struct NotEqCmpExpression : public BinaryExpression {};

        struct OrExpression : public BinaryExpression {};
        struct XorExpression : public BinaryExpression {};
        struct AndExpression : public BinaryExpression {};
        struct LTExpression : public BinaryExpression {};
        struct LTEExpression : public BinaryExpression {};
        struct GTExpression : public BinaryExpression {};
        struct GTEExpression : public BinaryExpression {};

        struct MetaCallExpr : public Expression {            
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct WhileStatement : public Statement {
            Statement* body;
            Expression* condition;
        };
    }
}