#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Optional.h>
#include <boost/variant.hpp>
#include <vector>
#include <stdexcept>
#include <functional>
#include <string>
#include <cassert>
#include <memory>

#ifndef _MSC_VER
#include <Wide/Semantic/Analyzer.h>
#endif
#pragma warning(disable : 4250)

namespace llvm {
    class Type;
    class LLVMContext;
    class Module;
}
namespace clang {
    class QualType;
}
namespace Wide {
    namespace ClangUtil {
        class ClangTU;
    }
    namespace Codegen {
        class Expression;
        class Generator;
    }
    namespace AST {
        struct DeclContext;
    }
    namespace Lexer {
        enum class TokenType : int;
    }
    namespace Semantic {
        class Analyzer;
        class OverloadSet;
        struct Type;
        struct DeferredExpression;
        struct Expression;
        struct ConcreteExpression;
        struct Context {
            Context(const Context& other)
                : a(other.a)
                , where(other.where)
                , RAIIHandler(other.RAIIHandler) {}
            Context(Context&& other)
                : a(other.a)
                , where(other.where)
                , RAIIHandler(std::move(other.RAIIHandler)) {}

            Context(Analyzer* an, Lexer::Range loc, std::function<void(ConcreteExpression)> handler)
                : a(an), where(loc), RAIIHandler(std::move(handler)) {}
            Context(Analyzer& an, Lexer::Range loc, std::function<void(ConcreteExpression)> handler)
                : a(&an), where(loc), RAIIHandler(std::move(handler)) {}
            Analyzer* a;
            Analyzer* operator->() const { return a; }
            Analyzer& operator*() const { return *a; }
            Lexer::Range where;
            void operator()(ConcreteExpression e);
            std::function<void(ConcreteExpression)> RAIIHandler;
        };
        struct ConcreteExpression {
            ConcreteExpression(Type* ty, Codegen::Expression* ex)
                : t(ty), Expr(ex), steal(false) {}
            Type* t;
            Codegen::Expression* Expr;
            bool steal;
            
            ConcreteExpression BuildValue(Context c);
            OverloadSet* AccessMember(Lexer::TokenType name, Context c);
            Wide::Util::optional<Expression> AccessMember(std::string name, Context c);
            ConcreteExpression BuildDereference(Context c);
            ConcreteExpression BuildIncrement(bool postfix, Context c);
            ConcreteExpression BuildNegate(Context c);
            Expression BuildCall(Context c);
            Expression BuildCall(ConcreteExpression arg, Context c);
            Expression BuildCall(ConcreteExpression lhs, ConcreteExpression rhs, Context c);
            Expression BuildCall(std::vector<ConcreteExpression> args, Context c);
            ConcreteExpression BuildMetaCall(std::vector<ConcreteExpression>, Context c);    
            Wide::Util::optional<Expression> PointerAccessMember(std::string name, Context c);
            ConcreteExpression AddressOf(Context c);
            Codegen::Expression* BuildBooleanConversion(Context c);
            Expression BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, Context c);
            
            Expression BuildCall(std::vector<Expression> args, std::vector<ConcreteExpression> destructors, Context c);
            Expression BuildCall(std::vector<Expression> args, Context c);
            Expression BuildCall(Expression lhs, Expression rhs, Context c);
            Expression BuildCall(Expression arg, Context c);
            Expression BuildMetaCall(std::vector<Expression> args, Context c);            
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Context c);
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c);
            Expression BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c);
            Expression BuildCall(std::vector<ConcreteExpression> arguments, std::vector<ConcreteExpression> destructors, Context c);
            DeferredExpression BuildCall(std::vector<DeferredExpression> args, Context c);
            DeferredExpression BuildCall(DeferredExpression lhs, DeferredExpression rhs, Context c);
            DeferredExpression BuildCall(DeferredExpression arg, Context c);
            DeferredExpression BuildMetaCall(std::vector<DeferredExpression> args, Context c);            
            DeferredExpression BuildBinaryExpression(DeferredExpression rhs, Lexer::TokenType type, Context c);
        };

        struct DeferredExpression {
            template<typename X> DeferredExpression(
                X&& other, 
                typename std::enable_if<
                    //std::is_same<
                    //    decltype(std::declval<X&>()(std::declval<Wide::Semantic::Type*>())), 
                    //    ConcreteExpression
                    //>::value &&
                    !std::is_same<DeferredExpression, typename std::decay<X>::type>::value
                >::type* = 0
            ) : delay(std::make_shared<std::function<ConcreteExpression(Type*)>>(std::forward<X>(other))) {
                // Should be SFINAE but VS2012 CTP compiler bug.
                static_assert(std::is_same<decltype(std::declval<X&>()(std::declval<Wide::Semantic::Type*>())), ConcreteExpression>::value, "");
            }

            DeferredExpression(const DeferredExpression& other)
                : delay(other.delay) {}
            DeferredExpression(DeferredExpression&& other)
                : delay(std::move(other.delay)) {}

            DeferredExpression BuildValue(Context c);
            DeferredExpression AccessMember(std::string name, Context c);
            DeferredExpression BuildDereference(Context c);
            DeferredExpression BuildIncrement(bool postfix, Context c);
            DeferredExpression BuildNegate(Context c);
            DeferredExpression BuildCall(Context c);
            DeferredExpression BuildCall(std::vector<Expression> args, std::vector<ConcreteExpression> destructors, Context c);
            DeferredExpression BuildCall(std::vector<Expression> args, Context c);
            DeferredExpression BuildCall(Expression lhs, Expression rhs, Context c);
            DeferredExpression BuildCall(Expression arg, Context c);
            DeferredExpression PointerAccessMember(std::string name, Context c);
            DeferredExpression AddressOf(Context c);
            DeferredExpression BuildBooleanConversion(Context c);
            DeferredExpression BuildMetaCall(std::vector<Expression> args, Context c);            
            DeferredExpression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Context c);   
            DeferredExpression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c);   

            ConcreteExpression operator()(Type* t) const {
                return (*delay)(t);
            }

            std::shared_ptr<std::function<ConcreteExpression(Type*)>> delay;
        };

        struct Expression {
            Expression(ConcreteExpression e)
                : contents(e) {}
            Expression(DeferredExpression e)
                : contents(e) {}

            boost::variant<ConcreteExpression, DeferredExpression> contents;
            template<typename A, typename B> auto VisitContents(A&& a, B&& b) -> decltype(a(*(ConcreteExpression*)nullptr)) {
                typedef decltype(a(*(ConcreteExpression*)nullptr)) result;
                struct visitation : public boost::static_visitor<result> {
                    typename std::decay<A>::type* a;
                    typename std::decay<B>::type* b;
                    result operator()(ConcreteExpression& expr) {
                        return (*a)(expr);
                    }
                    result operator()(DeferredExpression& expr) {
                        return (*b)(expr);
                    }
                };
                visitation v;
                v.a = &a;
                v.b = &b;
                return contents.apply_visitor(v);
            }
            template<typename A, typename B> auto VisitContents(A&& a, B&& b) const -> decltype(a(*(const ConcreteExpression*)nullptr))  {
                typedef decltype(a(*(ConcreteExpression*)nullptr)) result;
                struct visitation : public boost::static_visitor<result> {
                    A* a;
                    B* b;
                    result operator()(const ConcreteExpression& expr) {
                        return (*a)(expr);
                    }
                    result operator()(const DeferredExpression& expr) {
                        return (*b)(expr);
                    }
                };
                visitation v;
                v.a = &a;
                v.b = &b;
                return contents.apply_visitor(v);
            }

            ConcreteExpression Resolve(Type* t) const {
                return VisitContents(
                    [&](const ConcreteExpression& expr) {
                        return expr;
                    },
                    [&](const DeferredExpression& expr) {
                        return expr(t);
                    }
                );
            }
            ConcreteExpression Resolve(Type* t) {
                return VisitContents(
                    [&](ConcreteExpression& expr) {
                        return expr;
                     },
                    [&](DeferredExpression& expr) {
                        return expr(t);
                    }
                );
            }
            
            Expression BuildValue(Context c);
            Wide::Util::optional<Expression> AccessMember(std::string name, Context c);
            Expression BuildCall(std::vector<Expression> args, std::vector<ConcreteExpression> destructors, Context c);
            Expression BuildCall(std::vector<Expression> args, Context c);
            Expression BuildCall(Expression lhs, Expression rhs, Context c);
            Expression BuildCall(Expression arg, Context c);
            Expression BuildCall(Context c);
            Expression BuildMetaCall(std::vector<Expression> args, Context c);
            Expression BuildDereference(Context c);
            Expression BuildIncrement(bool postfix, Context c);
            Expression BuildNegate(Context c);
            
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Context c);
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c);

            Wide::Util::optional<Expression> PointerAccessMember(std::string name, Context c);

            Expression AddressOf(Context c);

            Expression BuildBooleanConversion(Context c);
        };
        struct Type  {
        public:
            virtual bool IsReference(Type* to) {
                return false;
            }
            virtual bool IsReference() {
                return false;
            } 
            virtual Type* Decay() {
                return this;
            }

            virtual Type* GetContext(Analyzer& a);

            virtual bool IsComplexType() { return false; }
            virtual clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            virtual std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) {
                throw std::runtime_error("This type has no LLVM counterpart.");
            }

            virtual bool IsMovable() { return true; }
            virtual bool IsCopyable() { return true; }

            virtual std::size_t size(Analyzer& a) { throw std::runtime_error("Attempted to size a type that does not have a run-time size."); }
            virtual std::size_t alignment(Analyzer& a) { throw std::runtime_error("Attempted to align a type that does not have a run-time alignment."); }

            virtual ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Context c);
            virtual ConcreteExpression BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c);
            virtual ConcreteExpression BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c);
            
            virtual ConcreteExpression BuildValueConstruction(ConcreteExpression arg, Context c);
            virtual ConcreteExpression BuildRvalueConstruction(ConcreteExpression arg, Context c);
            virtual ConcreteExpression BuildLvalueConstruction(ConcreteExpression args, Context c);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, ConcreteExpression args, Context c);

            virtual ConcreteExpression BuildValueConstruction(Context c);
            virtual ConcreteExpression BuildRvalueConstruction(Context c);
            virtual ConcreteExpression BuildLvalueConstruction(Context c);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, Context c);

            virtual ConcreteExpression BuildValue(ConcreteExpression lhs, Context c);
            
            virtual Wide::Util::optional<Expression> AccessStaticMember(std::string name, Context c) {
                throw std::runtime_error("This type does not have any static members.");
            }
            virtual Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Context c);
            virtual OverloadSet* AccessMember(ConcreteExpression e, Lexer::TokenType type, Context c);
            
            virtual OverloadSet* AccessStaticMember(Lexer::TokenType type, Context c) {
                throw std::runtime_error("This type does not have any static members.");
            }
            virtual Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c) {
                for(auto x : destructors)
                    c(x);
                return BuildCall(val, std::move(args), c);
            }
            virtual Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
                if (IsReference())
                    return Decay()->BuildCall(val, std::move(args), c);
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual ConcreteExpression BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Context c) {
                if (IsReference())
                    return Decay()->BuildBooleanConversion(val, c);
                throw std::runtime_error("Could not convert a type to boolean.");
            }
            
            virtual ConcreteExpression BuildNegate(ConcreteExpression val, Context c);
            virtual ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Context c) {
                if (IsReference())
                    return Decay()->BuildIncrement(obj, postfix, c);
                throw std::runtime_error("Attempted to increment a type that did not support it.");
            }    
            virtual ConcreteExpression BuildDereference(ConcreteExpression obj, Context c) {
                if (IsReference())
                    return Decay()->BuildDereference(obj, c);
                throw std::runtime_error("This type does not support de-referencing.");
            }
            virtual Wide::Util::optional<Expression> PointerAccessMember(ConcreteExpression obj, std::string name, Context c) {
                if (IsReference())
                    return Decay()->PointerAccessMember(obj, std::move(name), c);
                return obj.BuildDereference(c).AccessMember(std::move(name), c);
            }
            virtual ConcreteExpression AddressOf(ConcreteExpression obj, Context c);

            virtual Expression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Context c);
            virtual Expression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c);
            
            virtual OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c);
                                                
            virtual ~Type() {}
        };
        struct Callable : public virtual Type {
            virtual std::vector<Type*> GetArgumentTypes(Analyzer& a) = 0;
            virtual bool AddThis() { return false; }
        };
        class MetaType : public virtual Type {
        public:
            using Type::BuildValueConstruction;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) override;
        };
        template<typename F, typename T> Callable* make_assignment_callable(F f, T* self,  Context c) {            
            struct assign : public Callable, public MetaType {
                T* self;
                F action;
                assign(T* obj, F func)
                    : self(obj), action(std::move(func)) {}

                Expression BuildCall(ConcreteExpression lhs, std::vector<ConcreteExpression> args, Context c) override {
                    // Overload resolution should not pick us unless the args are a fit. Assert if we are picked and it's not correct.
                    assert(args.size() == 2);
                    assert(args[0].t = c->GetLvalueType(self));
                    assert(args[1].t->Decay() == self);
                    return action(args[0], args[1].BuildValue(c), c, self);
                }
                std::vector<Type*> GetArgumentTypes(Analyzer& a) override {
                    std::vector<Type*> out;
                    out.push_back(a.GetLvalueType(self));
                    out.push_back(self);
                    return out;
                }
            };
            return c->arena.Allocate<assign>(self, std::move(f));
        }
        template<typename F, typename T> Callable* make_value_callable(F f, T* self, Context c) {            
            struct assign : public Callable, public MetaType {
                T* self;
                F action;
                assign(T* obj, F func)
                    : self(obj), action(std::move(func)) {}
                Expression BuildCall(ConcreteExpression lhs, std::vector<ConcreteExpression> args, Context c) override {
                    // Overload resolution should not pick us unless the args are a fit. Assert if we are picked and it's not correct.
                    assert(args.size() == 2);
                    assert(args[0].BuildValue(c).t == args[1].BuildValue(c).t);
                    assert(args[0].BuildValue(c).t == self);
                    return action(args[0].BuildValue(c), args[1].BuildValue(c), c, self);
                }
                std::vector<Type*> GetArgumentTypes(Analyzer& a) override {
                    std::vector<Type*> out;
                    out.push_back(self);
                    out.push_back(self);
                    return out;
                }
            };
            return c->arena.Allocate<assign>(self, std::move(f));
        }
    }
}