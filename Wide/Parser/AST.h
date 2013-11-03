#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <unordered_set>
#include <memory>
#include <Wide/Lexer/Token.h>

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
        struct This : Expression {
            This(Lexer::Range r)
                : Expression(r) {}
        };
        struct DeclContext {
            DeclContext(std::string nam, Lexer::Range decl)
                : name(std::move(nam)), where() { where.push_back(decl); }
            std::vector<Wide::Lexer::Range> where;
            virtual ~DeclContext() {} 
            std::string name;
        };
        struct Function;
        struct FunctionOverloadSet {
            FunctionOverloadSet() {}
            std::unordered_set<Function*> functions;
        };
        struct Module : public DeclContext {
            Module(std::string nam, Lexer::Range where)
                : DeclContext(std::move(nam), where) {}
            std::unordered_map<std::string, DeclContext*> decls;
            std::unordered_map<std::string, FunctionOverloadSet*> functions;
            std::unordered_map<Lexer::TokenType, FunctionOverloadSet*> opcondecls;
        };
        struct Variable;
        struct Type : public DeclContext, Expression {
            Type(std::string name, Lexer::Range loc) : DeclContext(name, loc), Expression(loc) {}
            std::vector<Variable*> variables;
            std::unordered_map<std::string, FunctionOverloadSet*> Functions;
            std::unordered_map<Lexer::TokenType, FunctionOverloadSet*> opcondecls;
        };
        struct Identifier : Expression {
            Identifier(std::string nam, Lexer::Range loc)
                : Expression(loc), val(std::move(nam)) {}
            std::string val;
        };
        struct String : Expression {
            String(std::string str, Lexer::Range r)
                : Expression(r), val(std::move(str)) {}
            std::string val;
        };
        struct MemberAccess : Expression {
            MemberAccess(std::string nam, Expression* e, Lexer::Range r)
                : Expression(r), mem(std::move(nam)), expr(e) {}
            std::string mem;
            Expression* expr;
        };
        struct BinaryExpression : public Expression {
            BinaryExpression(Expression* l, Expression* r, Lexer::TokenType t)
                : Expression(l->location + r->location), lhs(l), rhs(r), type(t) {}
            Expression* lhs;
            Expression* rhs;
            Lexer::TokenType type;
        };
        struct FunctionArgument {
            FunctionArgument(Lexer::Range where)
                : location(std::move(where)) {}
             // May be null
            Expression* type;
            std::string name;
            Lexer::Range location;
        };
        struct Lambda : Expression {
            std::vector<FunctionArgument> args;
            std::vector<Statement*> statements;
            std::vector<Variable*> Captures;
            bool defaultref;
            Lambda(std::vector<Statement*> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<Variable*> caps)
                : Expression(r), args(std::move(arg)), statements(std::move(body)), Captures(std::move(caps)), defaultref(ref) {}
        };
        struct Function : DeclContext {
            Function(std::string nam, std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<Variable*> caps)
                : DeclContext(std::move(nam), loc), args(std::move(ar)), prolog(std::move(prolog)), statements(std::move(b)), initializers(std::move(caps)) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> prolog;
            std::vector<Statement*> statements;
            std::vector<Variable*> initializers;
        };
        struct FunctionCall : Expression {
            FunctionCall(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                : Expression(loc), callee(obj), args(std::move(arg)) {}
            Expression* callee;
            std::vector<Expression*> args;
        };
        /*struct QualifiedName {
            std::vector<std::string> components;
        };*/
        struct Using : public DeclContext {
            Using(std::string name, Expression* ex, Lexer::Range where)
                :  DeclContext(std::move(name), where), expr(ex) {}
            Expression* expr;
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : Statement(r), RetExpr(nullptr) {}
            Return(Expression* e, Lexer::Range r) :  Statement(r), RetExpr(e) {}
            Expression* RetExpr;
        };
        struct Variable : public Statement {
            Variable(std::string nam, Expression* expr, Lexer::Range r)
                : Statement(r), name(std::move(nam)), initializer(expr) {}
            std::string name;
            Expression* initializer;
        };
        struct Integer : public Expression {
            Integer(std::string val, Lexer::Range loc)
                :  Expression(loc), integral_value(std::move(val)) {}
            std::string integral_value;
        };
        struct CompoundStatement : public Statement {
            CompoundStatement(std::vector<Statement*> body, Lexer::Range loc)
                : Statement(loc), stmts(std::move(body)) {}
            std::vector<Statement*> stmts;
        };
        struct If : public Statement {
            If(Expression* c, Statement* t, Statement* f, Lexer::Range loc)
                :  Statement(loc), true_statement(t), false_statement(f), condition(c) {}
            Statement* true_statement;
            Statement* false_statement;
            Expression* condition;
        };
        struct Auto : public Expression {
            Auto(Lexer::Range loc)
                : Expression(loc) {}
        };
        struct UnaryExpression : public Expression {
            UnaryExpression(Expression* expr, Lexer::Range loc)
                : Expression(loc), ex(expr) {}
            Expression* ex;
        };
        struct PointerMemberAccess : public UnaryExpression {
            std::string member;
            PointerMemberAccess(std::string name, Expression* expr, Lexer::Range loc)
                : UnaryExpression(expr, loc), member(std::move(name)) {}
        };
        struct Dereference : public UnaryExpression {
            Dereference(Expression* e, Lexer::Range pos)
                : UnaryExpression(e, pos) {}
        };
        struct AddressOf : public UnaryExpression {
            AddressOf(Expression* e, Lexer::Range pos)
                : UnaryExpression(e, pos) {}
        };
        struct Negate : public UnaryExpression {
            Negate(Expression* e, Lexer::Range pos)
                : UnaryExpression(e, pos) {}
        };
        struct ErrorExpr : public Expression {
            ErrorExpr(Lexer::Range pos)
                : Expression(pos) {}
        };
        struct MetaCall : public Expression {        
            MetaCall(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                :  Expression(loc), callee(obj), args(std::move(arg)) {}    
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct While : public Statement {
            While(Statement* b, Expression* c, Lexer::Range loc)
                : Statement(loc), body(b), condition(c) {}
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
        static const std::shared_ptr<std::string> global_module_location;
    }
    namespace Parser {
        enum class Error : int;
    }
    namespace AST {
        struct Combiner {
            Module root;
            std::unordered_map<DeclContext*, std::unique_ptr<DeclContext>> owned_decl_contexts;
            std::unordered_map<FunctionOverloadSet*, std::unique_ptr<FunctionOverloadSet>> owned_overload_sets;
            std::unordered_map<Function*, Function*> inverse;

            std::function<void(std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>>)> error;
            std::unordered_set<Module*> modules;

            std::unordered_map<Module*, std::unordered_set<DeclContext*>> errors;
        public:
            Combiner(std::function<void(std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>>)> err)
                : root("global", Lexer::Range(global_module_location)), error(std::move(err)) {}

            Module* GetGlobalModule() { return &root; }

            void Add(Module* m);
            void Remove(Module* m);
        };
    }
}