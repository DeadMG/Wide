#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <algorithm>
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
        struct Throw : Statement {
            Throw(Lexer::Range where, Expression* e)
            : Statement(where), expr(e) {}
            Throw(Lexer::Range where)
                : Statement(where), expr() {}
            Expression* expr;
        };
        struct This : Expression {
            This(Lexer::Range r)
                : Expression(r) {}
        };
        struct DeclContext {
            DeclContext(Lexer::Range decl, Lexer::Access a)
                : where(), access(a) { where.push_back(decl); }
            Lexer::Access access;
            std::vector<Wide::Lexer::Range> where;
            virtual ~DeclContext() {} 
            //std::string name;
        };
        struct Function;
        struct FunctionOverloadSet : DeclContext {
            FunctionOverloadSet(Lexer::Range r) : DeclContext(r, Lexer::Access::Public) {}
            std::unordered_set<Function*> functions;
        };
        struct Module : public DeclContext {
            Module(Lexer::Range where, Lexer::Access a)
                : DeclContext(where, a) {}
            std::unordered_map<std::string, DeclContext*> decls;
            std::unordered_map<Lexer::TokenType, FunctionOverloadSet*> opcondecls;
        };
        struct Variable;
        struct Type : public DeclContext, Expression {
            Type(std::vector<Expression*> base, Lexer::Range loc, Lexer::Access a) : DeclContext(loc, a), Expression(loc), bases(base) {}
            std::vector<std::pair<Variable*, Lexer::Access>> variables;
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
            MemberAccess(std::string nam, Expression* e, Lexer::Range r, Lexer::Range mem)
                : Expression(r), mem(std::move(nam)), expr(e), memloc(mem) {}
            std::string mem;
            Expression* expr;
            Lexer::Range memloc;
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
            Lexer::Range where;
            FunctionBase(std::vector<FunctionArgument> a, std::vector<Statement*> s, Lexer::Range loc)
                : args(std::move(a)), statements(std::move(s)), where(loc) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> statements;
            virtual ~FunctionBase() {} // Need dynamic_cast.
        };
        struct Lambda : Expression, FunctionBase {
            std::vector<Variable*> Captures;
            bool defaultref;
            Lambda(std::vector<Statement*> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<Variable*> caps)
                : Expression(r), FunctionBase(std::move(arg), std::move(body), r), Captures(std::move(caps)), defaultref(ref) {}
        };
        struct Function : FunctionBase {
            Lexer::Access access;
            Function(std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, Lexer::Access a, bool dyn)
                : FunctionBase(std::move(ar), std::move(b), loc), access(a), prolog(std::move(prolog)), dynamic(dyn) {}
            std::vector<Statement*> prolog;
            bool dynamic;
        };
        struct Constructor : Function {
            Constructor(std::vector<Statement*> b, std::vector<Statement*> prolog, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<Variable*> caps, Lexer::Access a)
                : Function(std::move(b), std::move(prolog), loc, std::move(ar), a, false), initializers(std::move(caps)) {}
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
            Using(Expression* ex, Lexer::Range where, Lexer::Access a)
                :  DeclContext(where, a), expr(ex) {}
            Expression* expr;
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : Statement(r), RetExpr(nullptr) {}
            Return(Expression* e, Lexer::Range r) :  Statement(r), RetExpr(e) {}
            Expression* RetExpr;
        };
        struct Variable : public Statement {
            struct Name {
                std::string name;
                Lexer::Range where;
            };
            Variable(std::vector<Name> nam, Expression* expr, Lexer::Range r)
                : Statement(r), name(std::move(nam)), initializer(expr) {}
            std::vector<Name> name;
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
            Lexer::Range memloc;
            std::string member;
            PointerMemberAccess(std::string name, Expression* expr, Lexer::Range loc, Lexer::Range mem)
                : UnaryExpression(expr, loc), member(std::move(name)), memloc(mem) {}
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
        struct Decltype : UnaryExpression {
            Decltype(Expression* expr, Lexer::Range loc)
                : UnaryExpression(expr, loc) {}
        };
        struct Typeid : UnaryExpression {
            Typeid(Expression* expr, Lexer::Range loc)
            : UnaryExpression(expr, loc) {}
        };
        struct DynamicCast : Expression {
            Expression* type;
            Expression* object;
            DynamicCast(Expression* type, Expression* object, Lexer::Range where)
                : Expression(where), type(type), object(object) {}
        };
        struct MetaCall : public Expression {        
            MetaCall(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                :  Expression(loc), callee(obj), args(std::move(arg)) {}    
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct True : public Expression {
            True(Lexer::Range where) : Expression(where) {}
        };
        struct False : public Expression {
            False(Lexer::Range where) : Expression(where) {}
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
        struct TemplateType {
            Type* t;
            std::vector<FunctionArgument> arguments;
            TemplateType(Type* what, std::vector<FunctionArgument> args)
                : t(what), arguments(args) {}
        };
        struct TemplateTypeOverloadSet : DeclContext {
            TemplateTypeOverloadSet(Lexer::Range where) : DeclContext(where, Lexer::Access::Public) {}
            std::unordered_set<TemplateType*> templatetypes;
        };
        static const std::shared_ptr<std::string> global_module_location;
    }
    namespace Parser {
        enum class Error : int;
    }
    namespace AST {
        struct Combiner {
            std::unordered_set<Module*> modules;

            struct CombinedModule : public AST::Module {
                CombinedModule(Lexer::Range where, Lexer::Access a)
                : Module(where, a) {}

                std::unordered_map<std::string, std::unique_ptr<CombinedModule>> combined_modules;
                std::unordered_map<std::string, std::unique_ptr<FunctionOverloadSet>> combined_overload_sets;
                std::unordered_map<Lexer::TokenType, std::unique_ptr<FunctionOverloadSet>> operator_overload_sets;
                std::unordered_map<std::string, std::unordered_set<DeclContext*>> errors;

                void Add(DeclContext* d, std::string name);
                void Remove(DeclContext* d, std::string name);
                void AddModuleToSelf(Module* m);
                void RemoveModuleFromSelf(Module* m);
                void ReportErrors(std::function<void(std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>&)> error, std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>& scratch);
            };

            CombinedModule root;
            
        public:
            Combiner() : root(Lexer::Range(global_module_location), Lexer::Access::Public) {}

            Module* GetGlobalModule() { return &root; }

            void Add(Module* m);
            void Remove(Module* m);
            bool ContainsModule(Module* m) { return modules.find(m) != modules.end(); }
            void SetModules(std::unordered_set<Module*> modules);

            void ReportErrors(std::function<void(std::vector<std::pair<Wide::Lexer::Range*, Wide::AST::DeclContext*>>&)> error);
        };
    }
}