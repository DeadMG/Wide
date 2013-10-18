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
        struct Type;
        struct DeferredExpression;
        struct Expression;
        struct ConcreteExpression {
            ConcreteExpression()
                : t(nullptr)
                , Expr(nullptr)
                , steal(false) {}
            ConcreteExpression(Type* ty, Codegen::Expression* ex)
                : t(ty), Expr(ex), steal(false) {}
            Type* t;
            Codegen::Expression* Expr;
            bool steal;
            
            ConcreteExpression BuildValue(Analyzer& a, Lexer::Range where);
            Wide::Util::optional<ConcreteExpression> AccessMember(std::string name, Analyzer& a, Lexer::Range where);
            ConcreteExpression BuildDereference(Analyzer& a, Lexer::Range where);
            ConcreteExpression BuildIncrement(bool postfix, Analyzer& a, Lexer::Range where);
            ConcreteExpression BuildNegate(Analyzer& a, Lexer::Range where);
            Expression BuildCall(Analyzer& a, Lexer::Range where);
            Expression BuildCall(ConcreteExpression arg, Analyzer& a, Lexer::Range where);
            Expression BuildCall(ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where);
            Expression BuildCall(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);
            ConcreteExpression BuildMetaCall(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);    
            Wide::Util::optional<ConcreteExpression> PointerAccessMember(std::string name, Analyzer& a, Lexer::Range where);
            ConcreteExpression AddressOf(Analyzer& a, Lexer::Range where);
            Codegen::Expression* BuildBooleanConversion(Analyzer& a, Lexer::Range where);
            ConcreteExpression BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);

            Expression BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);
            Expression BuildCall(Expression lhs, Expression rhs, Analyzer& a, Lexer::Range where);
            Expression BuildCall(Expression arg, Analyzer& a, Lexer::Range where);
            Expression BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);            
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(std::vector<DeferredExpression> args, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(DeferredExpression lhs, DeferredExpression rhs, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(DeferredExpression arg, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildMetaCall(std::vector<DeferredExpression> args, Analyzer& a, Lexer::Range where);            
            DeferredExpression BuildBinaryExpression(DeferredExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);
        };

        struct DeferredExpression {
            DeferredExpression(std::function<ConcreteExpression(Type*)> d)
                : delay(std::make_shared<std::function<ConcreteExpression(Type*)>>(std::move(d))) {}

            DeferredExpression BuildValue(Analyzer& a, Lexer::Range where);
            DeferredExpression AccessMember(std::string name, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildDereference(Analyzer& a, Lexer::Range where);
            DeferredExpression BuildIncrement(bool postfix, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildNegate(Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(Expression lhs, Expression rhs, Analyzer& a, Lexer::Range where);
            DeferredExpression BuildCall(Expression arg, Analyzer& a, Lexer::Range where);
            DeferredExpression PointerAccessMember(std::string name, Analyzer& a, Lexer::Range where);
            DeferredExpression AddressOf(Analyzer& a, Lexer::Range where);
            DeferredExpression BuildBooleanConversion(Analyzer& a, Lexer::Range where);
            DeferredExpression BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);            
            DeferredExpression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);   

            ConcreteExpression operator()(Type* t) const {
                return (*delay)(t);
            }

            std::shared_ptr<std::function<ConcreteExpression(Type*)>> delay;
        };

        struct Expression {
            Expression()
                : contents(ConcreteExpression()) {}
            Expression(ConcreteExpression e)
                : contents(e) {}
            Expression(DeferredExpression e)
                : contents(e) {}

            boost::variant<ConcreteExpression, DeferredExpression> contents;
            template<typename A, typename B> auto VisitContents(A&& a, B&& b) -> decltype(a(*(ConcreteExpression*)nullptr)) {
                typedef decltype(a(*(ConcreteExpression*)nullptr)) result;
                struct visitation : public boost::static_visitor<result> {
                    A* a;
                    B* b;
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
                        return (*expr.delay)(t);
                    }
                );
            }
            ConcreteExpression Resolve(Type* t) {
                return VisitContents(
                    [&](ConcreteExpression& expr) {
                        return expr;
                     },
                    [&](DeferredExpression& expr) {
                        return (*expr.delay)(t);
                    }
                );
            }
            
            Expression BuildValue(Analyzer& a, Lexer::Range where);
            Wide::Util::optional<Expression> AccessMember(std::string name, Analyzer& a, Lexer::Range where);
            Expression BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);
            Expression BuildCall(Expression lhs, Expression rhs, Analyzer& a, Lexer::Range where);
            Expression BuildCall(Expression arg, Analyzer& a, Lexer::Range where);
            Expression BuildCall(Analyzer& a, Lexer::Range where);
            Expression BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where);
            Expression BuildDereference(Analyzer& a, Lexer::Range where);
            Expression BuildIncrement(bool postfix, Analyzer& a, Lexer::Range where);
            Expression BuildNegate(Analyzer& a, Lexer::Range where);
            
            Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);

            Wide::Util::optional<Expression> PointerAccessMember(std::string name, Analyzer& a, Lexer::Range where);

            Expression AddressOf(Analyzer& a, Lexer::Range where);

            Expression BuildBooleanConversion(Analyzer& a, Lexer::Range where);
        };

        enum ConversionRank {
            // No-cost conversion like reference binding or exact match
            Zero,

            // Derived-to-base conversion and such
            One,

            // User-defined implicit conversion
            Two,

            // No conversion possible
            None,
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

            virtual std::size_t size(Analyzer& a) { throw std::runtime_error("Attempted to size a type that does not have a run-time size."); }
            virtual std::size_t alignment(Analyzer& a) { throw std::runtime_error("Attempted to align a type that does not have a run-time alignment."); }

            virtual ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildRvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildLvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where);
            
            virtual ConcreteExpression BuildValueConstruction(ConcreteExpression arg, Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildRvalueConstruction(ConcreteExpression arg, Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildLvalueConstruction(ConcreteExpression args, Analyzer& a, Lexer::Range where);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, ConcreteExpression args, Analyzer& a, Lexer::Range where);

            virtual ConcreteExpression BuildValueConstruction(Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildRvalueConstruction(Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildLvalueConstruction(Analyzer& a, Lexer::Range where);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, Analyzer& a, Lexer::Range where);


            virtual ConcreteExpression BuildValue(ConcreteExpression lhs, Analyzer& a, Lexer::Range where);
            
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(Lexer::TokenType type, Analyzer& a, Lexer::Range where);
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(std::string name, Analyzer& a, Lexer::Range where);
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Analyzer& a, Lexer::Range where);
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression e, Lexer::TokenType type, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->AccessMember(e, type, a, where);
                return Wide::Util::none;
            }

            virtual Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->BuildCall(val, std::move(args), a, where);
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual ConcreteExpression BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual ConversionRank RankConversionFrom(Type* to, Analyzer& a);
            virtual Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->BuildBooleanConversion(val, a, where);
                throw std::runtime_error("Could not convert a type to boolean.");
            }
            
            virtual ConcreteExpression BuildNegate(ConcreteExpression val, Analyzer& a, Lexer::Range where);
            virtual ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->BuildIncrement(obj, postfix, a, where);
                throw std::runtime_error("Attempted to increment a type that did not support it.");
            }    
            virtual ConcreteExpression BuildDereference(ConcreteExpression obj, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->BuildDereference(obj, a, where);
                throw std::runtime_error("This type does not support de-referencing.");
            }
            virtual Wide::Util::optional<ConcreteExpression> PointerAccessMember(ConcreteExpression obj, std::string name, Analyzer& a, Lexer::Range where) {
                if (IsReference())
                    return Decay()->PointerAccessMember(obj, std::move(name), a, where);
                obj = obj.t->BuildDereference(obj, a, where);
                return obj.t->AccessMember(obj, std::move(name), a, where);
            }
            virtual ConcreteExpression AddressOf(ConcreteExpression obj, Analyzer& a, Lexer::Range where);

            virtual ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where);
                                                
            virtual ~Type() {}
        };
        struct Callable : public Type {
            virtual Type* GetReturnType() = 0;
            virtual std::vector<Type*> GetArgumentTypes() = 0;
        };
    }
}