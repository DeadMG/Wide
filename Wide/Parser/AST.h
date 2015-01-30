#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <typeindex>
#include <unordered_set>
#include <memory>
#include <Wide/Lexer/Token.h>
#include <boost/variant.hpp>
#include <boost/functional/hash.hpp>
#include <Wide/Util/Memory/MakeUnique.h>
#include <boost/version.hpp>

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
        template<typename T> using OverloadSet = std::unordered_map<Parse::Access, std::unordered_set<T>>;
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
        // If the first boolean is true, this object represents a collection of subobjects that can be unified.
        // If the second boolean is true, this object must be presented with the same access specifier everywhere.

        // Indicates that this object can be shared between multiple modules.
        // e.g., type, using.
        //struct Traversable;
        //struct Visitor {
        //    std::unordered_map<std::type_index, std::function<bool(const Traversable*)>> VisitFunctions;
        //    template<typename T, typename F> void AddVisitHandler(F f) {
        //        VisitFunctions[typeid(const T)] = [f](const Traversable* arg) {
        //            f(static_cast<const T*>(arg));
        //        }
        //    }
        //};
        //struct Traversable {
        //    // Need dynamic_cast.
        //    void Traverse(const Visitor& visitor) const {
        //        if (visitor.VisitFunctions.find(typeid(*this)) != visitor.VisitFunctions.end()) {
        //            if (visitor.VisitFunctions.at(typeid(*this))(this))
        //                return;
        //        }
        //        return TraverseNode(visitor);
        //    }
        //private:
        //    virtual void TraverseNode(const Visitor&) const = 0;
        //};
        struct SharedObjectTag {
            virtual Wide::Lexer::Range GetLocation() = 0;
            virtual ~SharedObjectTag() {}
        };
        // Indicates that this object is a container that contains multiple subobjects and must be unified.
        template<typename T> struct Container {
            virtual std::unordered_set<Wide::Lexer::Range> GetLocations() = 0;
            virtual std::unique_ptr<T> clone() const = 0;
            // REQUIRED STRONG EXCEPTION SAFETY
            virtual void unify(const T&) = 0;
            // REQUIRED NOTHROW
            virtual bool remove(const T&) = 0;
            virtual ~Container() {}
        };
        // The whole container has one access level.
        // e.g., module
        struct UniqueAccessContainer : Container<UniqueAccessContainer> {};
        // Each subobject has a distinct access level.
        struct MultipleAccessContainer : Container<MultipleAccessContainer> {};
        // Express common implementations of clone, unify, and remove.
        template<typename T, typename U> struct ContainerBase : U {
            std::unique_ptr<U> clone() const override final {
                return Wide::Memory::MakeUnique<T>(*static_cast<const T*>(this));
            }
            void unify(const U& arg) override final {
                if (auto targ = dynamic_cast<const T*>(&arg))
                    return static_cast<T*>(this)->unify(*targ);
                throw std::runtime_error("Could not unify.");
            }
            bool remove(const U& arg) override final {
                if (auto targ = dynamic_cast<const T*>(&arg))
                    return static_cast<T*>(this)->remove(*targ);
                throw std::runtime_error("Could not unify.");
            }
        };

        struct Statement /*: Traversable*/ {
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
            Import(std::unique_ptr<Expression> from, std::vector<Parse::Name> names, std::shared_ptr<Import> previous, std::vector<Parse::Name> hidden)
                : from(std::move(from)), names(names), previous(std::move(previous)), hidden(hidden) {}
            std::unique_ptr<Expression> from;
            std::vector<Parse::Name> names;
            std::shared_ptr<Import> previous;
            std::vector<Parse::Name> hidden;
        };
        struct Attribute {
            Attribute(const Attribute&) = delete;
            Attribute(Attribute&& other)
                : where(std::move(other.where))
                , initializer(std::move(other.initializer))
                , initialized(std::move(other.initialized)) {}
            Attribute(std::unique_ptr<Expression> begin, std::unique_ptr<Expression> end, Lexer::Range where)
                : where(where), initialized(std::move(begin)), initializer(std::move(end)) {}
            Lexer::Range where;
            std::unique_ptr<Expression> initializer;
            std::unique_ptr<Expression> initialized;
        };
        struct Throw : Statement {
            Throw(Lexer::Range where, std::unique_ptr<Expression> e)
                : Statement(where), expr(std::move(e)) {}
            Throw(Lexer::Range where)
                : Statement(where), expr() {}
            std::unique_ptr<Expression> expr;
            //void TraverseNode(const Visitor& visitor) const {
            //    if (expr)
            //        expr->Traverse(visitor);
            //}
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
        template<typename T> struct ModuleOverloadSet : ContainerBase<ModuleOverloadSet<T>, MultipleAccessContainer> {
            OverloadSet<std::shared_ptr<T>> funcs;
            std::unordered_set<Wide::Lexer::Range> GetLocations() {
                std::unordered_set<Wide::Lexer::Range> locations;
                for (auto&& f : funcs)
                    for (auto&& func : f.second)
                        locations.insert(func->where);
                return locations;
            }
            void unify(const ModuleOverloadSet<T>& with) {
                funcs.insert(with.funcs.begin(), with.funcs.end());
            }
            bool remove(const ModuleOverloadSet<T>& with) {
                for (auto&& item : with.funcs) {
                    for (auto&& ptr : item.second)
                        funcs[item.first].erase(ptr);
                    if (funcs[item.first].empty())
                        funcs.erase(item.first);
                }
                return funcs.empty();
            }
        };
    }
}

// Boost 1.55 and move-only types on VS don't play along.
#ifdef _MSC_VER
#if _MSC_VER >= 1600
#if BOOST_VERSION >= 105500
namespace boost {
    template<> struct is_nothrow_move_constructible<std::pair<Wide::Parse::Access, std::unique_ptr<Wide::Parse::Using>>> : public boost::true_type{};
    template<> struct is_nothrow_move_constructible<std::pair<Wide::Parse::Access, std::unique_ptr<Wide::Parse::UniqueAccessContainer>>> : public boost::true_type{};
    template<> struct is_nothrow_move_constructible<std::pair<Wide::Parse::Access, std::shared_ptr<Wide::Parse::SharedObjectTag>>> : public boost::true_type{};
    template<> struct is_nothrow_move_constructible<std::unique_ptr<Wide::Parse::MultipleAccessContainer>> : public boost::true_type{};
}
#endif
#endif
#endif
namespace Wide {
    namespace Parse {
        struct Module : ContainerBase<Module, UniqueAccessContainer> {
            Module() {}
            Module(const Module& other) {
                unify(other);
            }
            std::unordered_map<
                std::string,
                boost::variant<
                    std::pair<Parse::Access, std::unique_ptr<UniqueAccessContainer>>,
                    std::pair<Parse::Access, std::shared_ptr<SharedObjectTag>>,
                    std::unique_ptr<MultipleAccessContainer>
                >
            > named_decls;
            std::unordered_map<OperatorName, OverloadSet<std::shared_ptr<Function>>> OperatorOverloads;

            std::unordered_set<std::shared_ptr<Constructor>> constructor_decls;
            std::unordered_set<std::shared_ptr<Destructor>> destructor_decls;
            std::unordered_set<Lexer::Range> locations;
            std::unordered_set<Wide::Lexer::Range> GetLocations() {
                return locations;
            }

            void unify(const Module& with);
            bool remove(const Module& with);
        };

        struct MemberVariable {
            MemberVariable(const MemberVariable&) = delete;
            MemberVariable(MemberVariable&& other)
                : name(std::move(other.name))
                , where(std::move(other.where))
                , access(other.access)
                , initializer(std::move(other.initializer))
                , type(std::move(other.type))
                , attributes(std::move(other.attributes)) {}
            MemberVariable(std::string nam, std::unique_ptr<Expression> expr, Parse::Access access, Lexer::Range loc, std::vector<Attribute> attributes, std::unique_ptr<Expression> type)
                : name(std::move(nam)), initializer(std::move(expr)), where(loc), access(access), attributes(std::move(attributes)), type(std::move(type)) {}
            std::string name;
            Lexer::Range where;
            Parse::Access access;
            std::unique_ptr<Expression> initializer;
            std::unique_ptr<Expression> type;
            std::vector<Attribute> attributes;
        };
        struct FunctionArgument {
            FunctionArgument(const FunctionArgument&) = delete;
            FunctionArgument(FunctionArgument&& other)
                : type(std::move(other.type))
                , default_value(std::move(other.default_value))
                , name(std::move(other.name))
                , location(std::move(other.location)) {}
            FunctionArgument(Lexer::Range where, std::string name, std::unique_ptr<Expression> ty, std::unique_ptr<Expression> def)
                : location(std::move(where)), name(std::move(name)), type(std::move(ty)), default_value(std::move(def)) {}
            // May be null
            std::unique_ptr<Expression> type;
            std::unique_ptr<Expression> default_value;
            std::string name;
            Lexer::Range location;
        };
        struct FunctionBase {
            FunctionBase(const FunctionBase&) = delete;
            FunctionBase(FunctionBase&& other)
                : where(std::move(other.where))
                , args(std::move(other.args))
                , statements(std::move(other.statements)) {}
            FunctionBase(std::vector<FunctionArgument> a, std::vector<std::unique_ptr<Statement>> s, Lexer::Range loc)
                : args(std::move(a)), statements(std::move(s)), where(loc) {}
            Lexer::Range where;
            std::vector<FunctionArgument> args;
            std::vector<std::unique_ptr<Statement>> statements;
            virtual ~FunctionBase() {} // Need dynamic_cast.
        };
        struct AttributeFunctionBase : FunctionBase {
            AttributeFunctionBase(std::vector<std::unique_ptr<Statement>> b, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<Attribute> attributes)
                : FunctionBase(std::move(ar), std::move(b), loc), attributes(std::move(attributes)) {}
            std::vector<Attribute> attributes;
        };
        struct DynamicFunction : AttributeFunctionBase {
            DynamicFunction(std::vector<std::unique_ptr<Statement>> b, std::vector<FunctionArgument> args, Lexer::Range loc, std::vector<Attribute> attributes)
                : AttributeFunctionBase(std::move(b), loc, std::move(args), std::move(attributes)) {}
            bool dynamic = false;
        };
        struct Destructor : DynamicFunction {
            Destructor(std::vector<std::unique_ptr<Statement>> b, Lexer::Range loc, std::vector<Attribute> attributes, bool defaulted)
                : DynamicFunction(std::move(b), std::vector<FunctionArgument>(), loc, std::move(attributes)), defaulted(defaulted)
            {}
            bool defaulted = false;
        };
    }
}
#ifdef _MSC_VER
#if _MSC_VER >= 1600
#if BOOST_VERSION >= 105500
namespace boost {
    template<> struct is_nothrow_move_constructible<Wide::Parse::OverloadSet<std::unique_ptr<Wide::Parse::Function>>> : public boost::true_type{};
}
#endif
#endif
#endif
namespace Wide {
    namespace Parse {
        struct Type : Expression, SharedObjectTag {
            Type(const Type&) = delete;
            Type(Type&& other)
                : Expression(std::move(other))
                , variables(std::move(other.variables))
                , bases(std::move(other.bases))
                , nonvariables(std::move(other.nonvariables))
                , constructor_decls(std::move(other.constructor_decls))
                , destructor_decl(std::move(other.destructor_decl))
                , imports(std::move(other.imports))
                , attributes(std::move(other.attributes)) {}
            Type(std::vector<std::unique_ptr<Expression>> base, Lexer::Range loc, std::vector<Attribute> attributes)
                : Expression(loc), bases(std::move(base)), attributes(std::move(attributes)), destructor_decl(nullptr) {}
            std::vector<MemberVariable> variables;
            std::unordered_map<Name, 
                boost::variant<
                    OverloadSet<std::unique_ptr<Function>>, 
                    std::pair<Parse::Access, std::unique_ptr<Using>>
                >
            > nonvariables;
            OverloadSet<std::unique_ptr<Constructor>> constructor_decls;
            std::unique_ptr<Destructor> destructor_decl;
            std::vector<std::tuple<std::unique_ptr<Expression>, std::vector<Name>, bool>> imports;
            Wide::Lexer::Range GetLocation() {
                return location;
            }
            std::vector<std::unique_ptr<Expression>> bases;
            std::vector<Attribute> attributes;
        };
        struct Identifier : Expression {
            Identifier(Name nam, std::shared_ptr<Import> imp, Lexer::Range loc)
                : Expression(loc), imp(imp), val(std::move(nam)) {}
            std::shared_ptr<Import> imp;
            Name val;
        };
        struct String : Expression {
            String(std::string str, Lexer::Range r)
                : Expression(r), val(std::move(str)) {}
            std::string val;
        };
        struct MemberAccess : Expression {
            MemberAccess(Name nam, std::unique_ptr<Expression> e, Lexer::Range r, Lexer::Range mem)
                : Expression(r), mem(std::move(nam)), expr(std::move(e)), memloc(mem) {}
            Name mem;
            std::unique_ptr<Expression> expr;
            Lexer::Range memloc;
        };
        struct DestructorAccess : Expression {
            DestructorAccess(std::unique_ptr<Expression> e, Lexer::Range r)
            : Expression(r), expr(std::move(e)) {}
            std::unique_ptr<Expression> expr;
        };
        struct BinaryExpression : public Expression {
            BinaryExpression(std::unique_ptr<Expression> l, std::unique_ptr<Expression> r, Lexer::TokenType t)
                : Expression(l->location + r->location), lhs(std::move(l)), rhs(std::move(r)), type(t) {}
            std::unique_ptr<Expression> lhs;
            std::unique_ptr<Expression> rhs;
            Lexer::TokenType type;
        };
        struct Index : Expression {
            Index(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> ind, Lexer::Range where)
            : Expression(std::move(where)), object(std::move(obj)), index(std::move(ind)) {}
            std::unique_ptr<Expression> object;
            std::unique_ptr<Expression> index;
        };
        struct TemplateType {
            std::unique_ptr<Type> t;
            std::vector<FunctionArgument> arguments;
            Wide::Lexer::Range where;
            TemplateType(Wide::Lexer::Range where, std::unique_ptr<Type> what, std::vector<FunctionArgument> args)
                : where(where), t(std::move(what)), arguments(std::move(args)) {}
        };
        struct Variable : public Statement {
            Variable(const Variable&) = delete;
            Variable(Variable&& other)
                : Statement(std::move(other))
                , name(std::move(other.name))
                , type(std::move(other.type))
                , initializer(std::move(other.initializer)) {}
            struct Name {
                std::string name;
                Lexer::Range where;
            };
            Variable(std::vector<Name> nam, std::unique_ptr<Expression> expr, Lexer::Range r, std::unique_ptr<Expression> type)
                : Statement(r), name(std::move(nam)), initializer(std::move(expr)), type(std::move(type)) {}
            std::vector<Name> name;
            std::unique_ptr<Expression> type;
            std::unique_ptr<Expression> initializer;
            //void TraverseNode(const Visitor& visitor) const {
            //    if (type)
            //        type->Traverse(visitor);
            //    initializer->Traverse(visitor);
            //}
        };
        struct Lambda : Expression, FunctionBase {
            std::vector<Variable> Captures;
            bool defaultref;
            Lambda(const Lambda&) = delete;
            Lambda(Lambda&& other) 
                : Expression(std::move(other))
                , FunctionBase(std::move(other))
                , Captures(std::move(other.Captures))
                , defaultref(std::move(other.defaultref)) {}
            Lambda(std::vector<std::unique_ptr<Statement>> body, std::vector<FunctionArgument> arg, Lexer::Range r, bool ref, std::vector<Variable> caps)
                : Expression(r), FunctionBase(std::move(arg), std::move(body), r), Captures(std::move(caps)), defaultref(ref) {}
            //void TraverseNode(const Visitor& visitor) const {
            //    for (auto&& var : Captures)
            //        var.Traverse(visitor);
            //    for (auto&& stmt : statements)
            //        stmt->Traverse(visitor);
            //}
        };
        struct Function : DynamicFunction {
            Function(std::vector<std::unique_ptr<Statement>> b, Lexer::Range loc, std::vector<FunctionArgument> ar, std::unique_ptr<Expression> explicit_ret, std::vector<Attribute> attributes)
                : DynamicFunction(std::move(b), std::move(ar), loc, std::move(attributes)), explicit_return(std::move(explicit_ret)) {}
            std::unique_ptr<Expression> explicit_return = nullptr;
            bool abstract = false;
            bool deleted = false;
            bool defaulted = false;
        };
        struct VariableInitializer {
            VariableInitializer(const VariableInitializer&) = delete;
            VariableInitializer(VariableInitializer&& other)
                : where(other.where)
                , initializer(std::move(other.initializer))
                , initialized(std::move(other.initialized)) {}
            VariableInitializer(std::unique_ptr<Expression> begin, std::unique_ptr<Expression> end, Lexer::Range where)
            : where(where), initialized(std::move(begin)), initializer(std::move(end)) {}
            Lexer::Range where;
            std::unique_ptr<Expression> initializer;
            std::unique_ptr<Expression> initialized;
        };
        struct Constructor : AttributeFunctionBase {
            Constructor(std::vector<std::unique_ptr<Statement>> b, Lexer::Range loc, std::vector<FunctionArgument> ar, std::vector<VariableInitializer> caps, std::vector<Attribute> attributes)
            : AttributeFunctionBase(std::move(b), loc, std::move(ar), std::move(attributes)), initializers(std::move(caps)) {}
            std::vector<VariableInitializer> initializers;
            bool deleted = false;
            bool defaulted = false;
        };
        struct FunctionCall : Expression {
            FunctionCall(std::unique_ptr<Expression> obj, std::vector<std::unique_ptr<Expression>> arg, Lexer::Range loc)
                : Expression(loc), callee(std::move(obj)), args(std::move(arg)) {}
            std::unique_ptr<Expression> callee;
            std::vector<std::unique_ptr<Expression>> args;
        };
        struct Using : SharedObjectTag {
            Using(std::unique_ptr<Expression> ex, Lexer::Range where)
                :  location(where), expr(std::move(ex)) {}
            Lexer::Range location;
            std::unique_ptr<Expression> expr;
            Wide::Lexer::Range GetLocation() {
                return location;
            }
        };
        struct Return : public Statement {
            Return(Lexer::Range r) : Statement(r), RetExpr(nullptr) {}
            Return(std::unique_ptr<Expression> e, Lexer::Range r) : Statement(r), RetExpr(std::move(e)) {}
            std::unique_ptr<Expression> RetExpr; 
            //void TraverseNode(const Visitor& visitor) const {
            //    if (RetExpr)
            //        RetExpr->Traverse(visitor);
            //}
        };
        struct GlobalModuleReference : public Expression {
            GlobalModuleReference(Lexer::Range where) : Expression(where) {}
        };
        struct Integer : public Expression {
            Integer(std::string val, Lexer::Range loc)
                :  Expression(loc), integral_value(std::move(val)) {}
            std::string integral_value;
        };
        struct CompoundStatement : public Statement {
            CompoundStatement(std::vector<std::unique_ptr<Statement>> body, Lexer::Range loc)
                : Statement(loc), stmts(std::move(body)) {}
            //void TraverseNode(const Visitor& visitor) const {
            //    for (auto&& stmt : stmts)
            //        stmt->Traverse(visitor);
            //}
            std::vector<std::unique_ptr<Statement>> stmts;
        };
        struct Catch {
            Catch(const Catch&) = delete;
            Catch(Catch&& other) 
                : name(std::move(other.name))
                , type(std::move(other.type))
                , all(std::move(other.all))
                , statements(std::move(other.statements)) {}
            Catch(std::vector<std::unique_ptr<Statement>> statements)
            : statements(std::move(statements)), all(true) {}
            Catch(std::vector<std::unique_ptr<Statement>> statements, std::string name, std::unique_ptr<Expression> type)
                : statements(std::move(statements)), all(false), name(name), type(std::move(type)) {}
            std::string name;
            std::unique_ptr<Expression> type = nullptr;
            bool all;
            std::vector<std::unique_ptr<Statement>> statements;
        };
        struct TryCatch : public Statement {
            TryCatch(TryCatch&& other)
                : Statement(std::move(other))
                , statements(std::move(other.statements))
                , catches(std::move(other.catches)) {}
            TryCatch(const TryCatch&) = delete;
            TryCatch(std::unique_ptr<CompoundStatement> stmt, std::vector<Catch> catches, Lexer::Range range)
            : statements(std::move(stmt)), catches(std::move(catches)), Statement(range) {}
            std::unique_ptr<CompoundStatement> statements;
            std::vector<Catch> catches;
            //void TraverseNode(const Visitor& visitor) const {
            //
            //}
        };
        struct If : public Statement {
            If(std::unique_ptr<Expression> c, std::unique_ptr<Statement> t, std::unique_ptr<Statement> f, Lexer::Range loc)
                :  Statement(loc), true_statement(std::move(t)), false_statement(std::move(f)), condition(std::move(c)), var_condition(nullptr) {}
            If(std::unique_ptr<Variable> c, std::unique_ptr<Statement> t, std::unique_ptr<Statement> f, Lexer::Range loc)
                :  Statement(loc), true_statement(std::move(t)), false_statement(std::move(f)), var_condition(std::move(c)), condition(nullptr) {}
            std::unique_ptr<Statement> true_statement;
            std::unique_ptr<Statement> false_statement;
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Variable> var_condition;
        };
        struct Auto : public Expression {
            Auto(Lexer::Range loc)
                : Expression(loc) {}
        };
        struct UnaryExpression : public Expression {
            UnaryExpression(std::unique_ptr<Expression> expr, Lexer::TokenType type, Lexer::Range loc)
                : Expression(loc), ex(std::move(expr)), type(type) {}
            Lexer::TokenType type;
            std::unique_ptr<Expression> ex;
        };
        struct BooleanTest : public Expression {
            std::unique_ptr<Expression> ex;
            BooleanTest(std::unique_ptr<Expression> e, Lexer::Range where)
            : Expression(where), ex(std::move(e)) {}
        };
        struct PointerMemberAccess : public Expression {
            Lexer::Range memloc;
            Name member;
            std::unique_ptr<Expression> ex;
            PointerMemberAccess(Name name, std::unique_ptr<Expression> expr, Lexer::Range loc, Lexer::Range mem)
                : Expression(loc), member(std::move(name)), memloc(mem), ex(std::move(expr)) {}
        };
        struct PointerDestructorAccess : public Expression {
            std::unique_ptr<Expression> ex;
            PointerDestructorAccess(std::unique_ptr<Expression> expr, Lexer::Range loc)
                : Expression(loc), ex(std::move(expr)) {}
        };
        struct Decltype : Expression {
            std::unique_ptr<Expression> ex;
            Decltype(std::unique_ptr<Expression> expr, Lexer::Range loc)
                : Expression(loc), ex(std::move(expr)) {}
        };
        struct Typeid : Expression {
            std::unique_ptr<Expression> ex;
            Typeid(std::unique_ptr<Expression> expr, Lexer::Range loc)
            : Expression(loc), ex(std::move(expr)) {}
        };
        struct DynamicCast : Expression {
            std::unique_ptr<Expression> type;
            std::unique_ptr<Expression> object;
            DynamicCast(std::unique_ptr<Expression> type, std::unique_ptr<Expression> object, Lexer::Range where)
                : Expression(where), type(std::move(type)), object(std::move(object)) {}
        };
        struct MetaCall : public Expression {        
            MetaCall(std::unique_ptr<Expression> obj, std::vector<std::unique_ptr<Expression>> arg, Lexer::Range loc)
                :  Expression(loc), callee(std::move(obj)), args(std::move(arg)) {}    
            std::unique_ptr<Expression> callee;
            std::vector<std::unique_ptr<Expression>> args;
        };
        struct True : public Expression {
            True(Lexer::Range where) : Expression(where) {}
        };
        struct False : public Expression {
            False(Lexer::Range where) : Expression(where) {}
        };
        struct While : public Statement {
            While(std::unique_ptr<Statement> b, std::unique_ptr<Expression> c, Lexer::Range loc)
                : Statement(loc), body(std::move(b)), condition(std::move(c)), var_condition(nullptr) {}
            While(std::unique_ptr<Statement> b, std::unique_ptr<Variable> c, Lexer::Range loc)
                : Statement(loc), body(std::move(b)), var_condition(std::move(c)), condition(nullptr) {}
            std::unique_ptr<Statement> body;
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Variable> var_condition;
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
            Increment(std::unique_ptr<Expression> ex, Lexer::Range r, bool post)
                : UnaryExpression(std::move(ex), &Lexer::TokenTypes::Increment, r), postfix(post) {}
        };
        struct Decrement : public UnaryExpression {
            bool postfix;
            Decrement(std::unique_ptr<Expression> ex, Lexer::Range r, bool post)
                : UnaryExpression(std::move(ex), &Lexer::TokenTypes::Decrement, r), postfix(post) {}
        };
        struct Tuple : public Expression {
            std::vector<std::unique_ptr<Expression>> expressions;

            Tuple(std::vector<std::unique_ptr<Expression>> exprs, Lexer::Range where)
                : expressions(std::move(exprs)), Expression(where) {}
        };
        struct Combiner {
            std::unordered_set<Module*> modules;
            
            Module root;            
        public:
            Combiner() {}

            Module* GetGlobalModule() { return &root; }

            void Add(Module* m);
            void Remove(Module* m);
            bool ContainsModule(Module* m) { return modules.find(m) != modules.end(); }
            void SetModules(std::unordered_set<Module*> mods);
        };
    }
}