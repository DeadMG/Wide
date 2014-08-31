#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <unordered_set>
#include <memory>
#include <Wide/Lexer/Token.h>
#include <boost/variant.hpp>
#include <boost/functional/hash.hpp>

namespace Wide {
    namespace Parse {
        enum Access {
            Public,
            Protected,
            Private,
        };
    }
}
namespace std {
    template<> struct hash<Wide::Parse::Access> {
        std::size_t operator()(Wide::Parse::Access ty) const {
            return std::hash<int>()((int)ty);
        }
    };
}
namespace Wide {
    namespace Parse {
        typedef std::vector<Wide::Lexer::TokenType> OperatorName;
        typedef boost::variant<std::string, OperatorName> Name;
        template<typename T> using OverloadSet = std::unordered_map<Parse::Access, std::unordered_set<T*>>;
    }
}
namespace boost {
    template<> struct hash<Wide::Parse::OperatorName>{
        std::size_t operator()(const Wide::Parse::OperatorName& ty) const {
            return boost::hash_range(ty.begin(), ty.end());
        }
    };
}
namespace std {
    template<> struct hash<Wide::Parse::OperatorName>{
        std::size_t operator()(const Wide::Parse::OperatorName& ty) const {
            return boost::hash_range(ty.begin(), ty.end());
        }
    };
    template<> struct hash<Wide::Parse::Name> {
        std::size_t operator()(Wide::Parse::Name ty) const {
            return boost::hash_value(ty);
        }
    };
}
namespace Wide {
    namespace Parse {
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
        struct Import {
            Import(Expression* from, std::vector<Parse::Name> names, Import* previous, std::vector<Parse::Name> hidden)
                : from(from), names(names), previous(previous), hidden(hidden) {}
            Expression* from;
            std::vector<Parse::Name> names;
            Import* previous; 
            std::vector<Parse::Name> hidden;
        };
        struct Attribute {
            Attribute(Expression* begin, Expression* end, Lexer::Range where)
                : where(where), initialized(begin), initializer(end) {}
            Lexer::Range where;
            Expression* initializer;
            Expression* initialized;
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
        struct Function;
        struct TemplateType;
        struct Type;
        struct Using;
        struct Destructor;
        struct Constructor;
        struct Module {
            Module() {}
            std::unordered_map<
                std::string,
                boost::variant<
                    std::pair<Parse::Access, Module*>,
                    std::pair<Parse::Access, Type*>,
                    std::pair<Parse::Access, Using*>,
                    OverloadSet<Function>,
                    OverloadSet<TemplateType>
                >
            > named_decls;
            std::unordered_map<OperatorName, OverloadSet<Function>> OperatorOverloads;

            std::unordered_set<Constructor*> constructor_decls;
            std::unordered_set<Destructor*> destructor_decls;
            std::vector<Lexer::Range> locations;
            virtual ~Module() {}
        };

        struct MemberVariable {
            MemberVariable(std::string nam, Expression* expr, Parse::Access access, Lexer::Range loc, std::vector<Attribute> attributes)
                : name(std::move(nam)), initializer(expr), where(loc), access(access), attributes(std::move(attributes)) {}
            std::string name;
            Lexer::Range where;
            Parse::Access access;
            Expression* initializer;
            std::vector<Attribute> attributes;
        };
        struct Destructor;
        struct Type : Expression {
            Type(std::vector<Expression*> base, Lexer::Range loc, std::vector<Attribute> attributes) : Expression(loc), bases(base), attributes(attributes), destructor_decl(nullptr) {}
            std::vector<MemberVariable> variables;
            std::unordered_map<Name, boost::variant<OverloadSet<Function>, std::pair<Parse::Access, Using*>>> nonvariables;
            OverloadSet<Constructor> constructor_decls;
            Destructor* destructor_decl;
            std::vector<std::tuple<Expression*, std::vector<Name>, bool>> imports;

            std::vector<Expression*> bases;
            std::vector<Attribute> attributes;
        };
        struct Identifier : Expression {
            Identifier(Name nam, Import* imp, Lexer::Range loc)
                : Expression(loc), imp(imp), val(std::move(nam)) {}
            Import* imp;
            Name val;
        };
        struct String : Expression {
            String(std::string str, Lexer::Range r)
                : Expression(r), val(std::move(str)) {}
            std::string val;
        };
        struct MemberAccess : Expression {
            MemberAccess(Name nam, Expression* e, Lexer::Range r, Lexer::Range mem)
                : Expression(r), mem(std::move(nam)), expr(e), memloc(mem) {}
            Name mem;
            Expression* expr;
            Lexer::Range memloc;
        };
        struct DestructorAccess : Expression {
            DestructorAccess(Expression* e, Lexer::Range r)
            : Expression(r), expr(e) {}
            Expression* expr;
        };
        struct BinaryExpression : public Expression {
            BinaryExpression(Expression* l, Expression* r, Lexer::TokenType t)
                : Expression(l->location + r->location), lhs(l), rhs(r), type(t) {}
            Expression* lhs;
            Expression* rhs;
            Lexer::TokenType type;
        };
        struct Index : Expression {
            Index(Expression* obj, Expression* ind, Lexer::Range where)
            : Expression(where), object(obj), index(ind) {}
            Expression* object;
            Expression* index;
        };
        struct FunctionArgument {
            FunctionArgument(Lexer::Range where, std::string name, Expression* ty)
                : location(std::move(where)), name(std::move(name)), type(ty) {}
            FunctionArgument(Lexer::Range where, std::string name)
                : location(std::move(where)), name(std::move(name)), type(nullptr) {}
             // May be null
            Expression* type;
            std::string name;
            Lexer::Range location;
        };
        struct TemplateType {
            Type* t;
            std::vector<FunctionArgument> arguments;
            TemplateType(Type* what, std::vector<FunctionArgument> args)
                : t(what), arguments(args) {}
        };
        struct FunctionBase {
            Lexer::Range where;
            FunctionBase(std::vector<FunctionArgument> a, std::vector<Statement*> s, Lexer::Range loc)
                : args(std::move(a)), statements(std::move(s)), where(loc) {}
            std::vector<FunctionArgument> args;
            std::vector<Statement*> statements;
            virtual ~FunctionBase() {} // Need dynamic_cast.
        };
        struct AttributeFunctionBase : FunctionBase {
            AttributeFunctionBase(std::vector<Statement*> b, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<Attribute> attributes)
            : FunctionBase(std::move(ar), std::move(b), loc), attributes(attributes) {}
            std::vector<Attribute> attributes;
        };
        struct Variable;
        struct Lambda : Expression, FunctionBase {
            std::vector<Variable*> Captures;
            bool defaultref;
            Lambda(std::vector<Statement*> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<Variable*> caps)
                : Expression(r), FunctionBase(std::move(arg), std::move(body), r), Captures(std::move(caps)), defaultref(ref) {}
        };
        struct DynamicFunction : AttributeFunctionBase {
            DynamicFunction(std::vector<Statement*> b, std::vector<FunctionArgument> args, Lexer::Range loc, std::vector<Attribute> attributes)
            : AttributeFunctionBase(std::move(b), loc, args, std::move(attributes)) {}
            bool dynamic = false;
        };
        struct Function : DynamicFunction {
            Function(std::vector<Statement*> b, Lexer::Range loc, std::vector<FunctionArgument> ar, Expression* explicit_ret, std::vector<Attribute> attributes)
                : DynamicFunction(std::move(b), std::move(ar), loc, std::move(attributes)), explicit_return(explicit_ret) {}
            Expression* explicit_return = nullptr;
            bool abstract = false;
            bool deleted = false;
            bool defaulted = false;
        };
        struct Destructor : DynamicFunction {
            Destructor(std::vector<Statement*> b, Lexer::Range loc, std::vector<Attribute> attributes, bool defaulted)
            : DynamicFunction(std::move(b), std::vector<FunctionArgument>(), loc, std::move(attributes)), defaulted(defaulted)
            {}
            bool defaulted = false;
        };
        struct VariableInitializer {
            VariableInitializer(Expression* begin, Expression* end, Lexer::Range where)
            : where(where), initialized(begin), initializer(end) {}
            Lexer::Range where;
            Expression* initializer;
            Expression* initialized;
        };
        struct Constructor : AttributeFunctionBase {
            Constructor(std::vector<Statement*> b, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<VariableInitializer> caps, std::vector<Attribute> attributes)
            : AttributeFunctionBase(std::move(b), loc, std::move(ar), std::move(attributes)), initializers(std::move(caps)) {}
            std::vector<VariableInitializer> initializers;
            bool deleted = false;
            bool defaulted = false;
        };
        struct FunctionCall : Expression {
            FunctionCall(Expression* obj, std::vector<Expression*> arg, Lexer::Range loc)
                : Expression(loc), callee(obj), args(std::move(arg)) {}
            Expression* callee;
            std::vector<Expression*> args;
        };
        struct Using {
            Using(Expression* ex, Lexer::Range where)
                :  location(where), expr(ex) {}
            Lexer::Range location;
            Expression* expr;
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : Statement(r), RetExpr(nullptr) {}
            Return(Expression* e, Lexer::Range r) :  Statement(r), RetExpr(e) {}
            Expression* RetExpr;
        };
        struct GlobalModuleReference : public Expression {
            GlobalModuleReference(Lexer::Range where) : Expression(where) {}
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
        struct Catch {
            Catch(std::vector<Statement*> statements)
            : statements(std::move(statements)), all(true) {}
            Catch(std::vector<Statement*> statements, std::string name, Expression* type)
                : statements(std::move(statements)), all(false), name(name), type(type) {}
            std::string name;
            Expression* type = nullptr;
            bool all;
            std::vector<Statement*> statements;
        };
        struct TryCatch : public Statement {
            TryCatch(CompoundStatement* stmt, std::vector<Catch> catches, Lexer::Range range)
            : statements(stmt), catches(catches), Statement(range) {}
            CompoundStatement* statements;
            std::vector<Catch> catches;
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
            UnaryExpression(Expression* expr, Lexer::TokenType type, Lexer::Range loc)
                : Expression(loc), ex(expr), type(type) {}
            Lexer::TokenType type;
            Expression* ex;
        };
        struct BooleanTest : public Expression {
            Expression* ex;
            BooleanTest(Expression* e, Lexer::Range where)
            : Expression(where), ex(e) {}
        };
        struct PointerMemberAccess : public Expression {
            Lexer::Range memloc;
            Name member;
            Expression* ex;
            PointerMemberAccess(Name name, Expression* expr, Lexer::Range loc, Lexer::Range mem)
                : Expression(loc), member(std::move(name)), memloc(mem), ex(expr) {}
        };
        struct PointerDestructorAccess : public Expression {
            Expression* ex;
            PointerDestructorAccess(Expression* expr, Lexer::Range loc)
                : Expression(loc), ex(expr) {}
        };
        struct ErrorExpr : public Expression {
            ErrorExpr(Lexer::Range pos)
                : Expression(pos) {}
        };
        struct Decltype : Expression {
            Expression* ex;
            Decltype(Expression* expr, Lexer::Range loc)
                : Expression(loc), ex(expr) {}
        };
        struct Typeid : Expression {
            Expression* ex;
            Typeid(Expression* expr, Lexer::Range loc)
            : Expression(loc), ex(expr) {}
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
                : UnaryExpression(ex, &Lexer::TokenTypes::Increment, r), postfix(post) {}
        };
        struct Decrement : public UnaryExpression {
            bool postfix;
            Decrement(Expression* ex, Lexer::Range r, bool post)
                : UnaryExpression(ex, &Lexer::TokenTypes::Decrement, r), postfix(post) {}
        };
        struct Tuple : public Expression {
            std::vector<Expression*> expressions;

            Tuple(std::vector<Expression*> exprs, Lexer::Range where)
                : expressions(std::move(exprs)), Expression(where) {}
        };
        struct Combiner {
            std::unordered_set<Module*> modules;

            struct CombinedModule : public Module {
                CombinedModule()
                : Module() {}
                std::unordered_map<std::string, std::unique_ptr<CombinedModule>> combined_modules;

                void AddModuleToSelf(Module* m);
                void RemoveModuleFromSelf(Module* m);
            };

            CombinedModule root;
            
        public:
            Combiner() {}

            Module* GetGlobalModule() { return &root; }

            void Add(Module* m);
            void Remove(Module* m);
            bool ContainsModule(Module* m) { return modules.find(m) != modules.end(); }
            void SetModules(std::unordered_set<Module*> modules);
        };
    }
}