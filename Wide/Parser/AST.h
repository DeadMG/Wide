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
            DeclContext(Lexer::Range decl)
                : where() { where.push_back(decl); }
            std::vector<Wide::Lexer::Range> where;
            virtual ~DeclContext() {} 
            //std::string name;
        };
        struct Function;
        struct FunctionOverloadSet {
            FunctionOverloadSet() {}
            std::unordered_set<Function*> functions;
        };
        struct Module : public DeclContext {
            Module(Lexer::Range where)
                : DeclContext(where) {}
            std::unordered_map<std::string, DeclContext*> decls;
            std::unordered_map<std::string, FunctionOverloadSet*> functions;
            std::unordered_map<Lexer::TokenType, FunctionOverloadSet*> opcondecls;
        };
        struct Variable;
        struct Type : public DeclContext, Expression {
            Type(std::vector<Expression*> base, Lexer::Range loc) : DeclContext(loc), Expression(loc), bases(base) {}
            std::vector<Variable*> variables;
            std::unordered_map<std::string, FunctionOverloadSet*> Functions;
            std::unordered_map<Lexer::TokenType, FunctionOverloadSet*> opcondecls;
            std::vector<Expression*> bases;
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
        struct FunctionBase {
            FunctionBase(std::vector<FunctionArgument> a, std::vector<Statement*> s)
                : args(std::move(a)), statements(std::move(s)) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> statements;
            virtual ~FunctionBase() {} // Need dynamic_cast.
            virtual Lexer::Range where() const = 0;
        };
        struct Lambda : Expression, FunctionBase {
            Lexer::Range where() const override final { return location; }
            std::vector<Variable*> Captures;
            bool defaultref;
            Lambda(std::vector<Statement*> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<Variable*> caps)
                : Expression(r), FunctionBase(std::move(arg), std::move(body)), Captures(std::move(caps)), defaultref(ref) {}
        };
        struct Function : DeclContext, FunctionBase {
            Lexer::Range where() const override final { return DeclContext::where.front(); }
            Function(std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar)
                : FunctionBase(std::move(ar), std::move(b)), DeclContext(loc), prolog(std::move(prolog)) {}
            std::vector<Statement*> prolog;
        };
        struct Constructor : Function {
            Constructor(std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<Variable*> caps)
                : Function(std::move(b), std::move(prolog), loc, std::move(ar)), initializers(std::move(caps)) {}
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
            Using(Expression* ex, Lexer::Range where)
                :  DeclContext(where), expr(ex) {}
            Expression* expr;
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : Statement(r), RetExpr(nullptr) {}
            Return(Expression* e, Lexer::Range r) :  Statement(r), RetExpr(e) {}
            Expression* RetExpr;
        };
        struct Variable : public Statement {
            Variable(std::vector<std::string> nam, Expression* expr, Lexer::Range r)
                : Statement(r), name(std::move(nam)), initializer(expr) {}
            std::vector<std::string> name;
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
                :  Statement(loc), true_statement(t), false_statement(f), condition(c), var_condition(nullptr) {}
            If(Variable* c, Statement* t, Statement* f, Lexer::Range loc)
                :  Statement(loc), true_statement(t), false_statement(f), var_condition(c), condition(nullptr) {}
            Statement* true_statement;
            Statement* false_statement;
            Expression* condition;
            Variable* var_condition;
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
                : Statement(loc), body(b), condition(c), var_condition(nullptr) {}
            While(Statement* b, Variable* c, Lexer::Range loc)
                : Statement(loc), body(b), var_condition(c), condition(nullptr) {}
            Statement* body;
            Expression* condition;
            Variable* var_condition;
        };
        struct Continue : public Statement {
            Continue(Lexer::Range where)
                : Statement(where) {}
        };
        struct Break : public Statement {
            Break(Lexer::Range where)
                : Statement(where) {}
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
        struct Tuple : public Expression {
            std::vector<Expression*> expressions;

            Tuple(std::vector<Expression*> exprs, Lexer::Range where)
                : expressions(std::move(exprs)), Expression(where) {}
        };
        static const std::shared_ptr<std::string> global_module_location;
    }
    namespace Parser {
        enum class Error : int;
    }
    namespace AST {
        struct Combiner {
            Module root;
            std::unordered_map<DeclContext*, std::unique_ptr<Module>> owned_decl_contexts;
            std::unordered_map<FunctionOverloadSet*, std::unique_ptr<FunctionOverloadSet>> owned_overload_sets;

            std::function<void(std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>>)> error;
            std::unordered_set<Module*> modules;

            std::unordered_map<Module*, std::unordered_map<std::string, std::unordered_set<DeclContext*>>> errors;
        public:
            Combiner(std::function<void(std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>>)> err)
                : root(Lexer::Range(global_module_location)), error(std::move(err)) {}

            Module* GetGlobalModule() { return &root; }

            void Add(Module* m);
            void Remove(Module* m);
        };
    }
}