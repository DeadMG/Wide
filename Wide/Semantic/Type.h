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
            Wide::Util::optional<ConcreteExpression> AccessMember(std::string name, Context c);
            ConcreteExpression BuildDereference(Context c);
            ConcreteExpression BuildIncrement(bool postfix, Context c);
            ConcreteExpression BuildNegate(Context c);
            ConcreteExpression BuildCall(Context c);
            ConcreteExpression BuildCall(ConcreteExpression arg, Context c);
            ConcreteExpression BuildCall(ConcreteExpression lhs, ConcreteExpression rhs, Context c);
            ConcreteExpression BuildCall(std::vector<ConcreteExpression> args, Context c);
            ConcreteExpression BuildCall(std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c);
            ConcreteExpression BuildMetaCall(std::vector<ConcreteExpression>, Context c);

            Wide::Util::optional<ConcreteExpression> PointerAccessMember(std::string name, Context c);
            ConcreteExpression AddressOf(Context c);
            Codegen::Expression* BuildBooleanConversion(Context c);
            ConcreteExpression BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, Context c);
            
            ConcreteExpression BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c);
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

            virtual bool IsMovable(Analyzer& a) { return !IsComplexType(); }
            virtual bool IsCopyable(Analyzer& a) { return !IsComplexType(); }

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
            
            virtual Wide::Util::optional<ConcreteExpression> AccessStaticMember(std::string name, Context c) {
                throw std::runtime_error("This type does not have any static members.");
            }
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c);
            virtual OverloadSet* AccessMember(ConcreteExpression e, Lexer::TokenType type, Context c);
            
            virtual OverloadSet* AccessStaticMember(Lexer::TokenType type, Context c) {
                throw std::runtime_error("This type does not have any static members.");
            }
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c) {
                for(auto x : destructors)
                    c(x);
                return BuildCall(val, std::move(args), c);
            }
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
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
            virtual Wide::Util::optional<ConcreteExpression> PointerAccessMember(ConcreteExpression obj, std::string name, Context c) {
                if (IsReference())
                    return Decay()->PointerAccessMember(obj, std::move(name), c);
                return obj.BuildDereference(c).AccessMember(std::move(name), c);
            }
            virtual ConcreteExpression AddressOf(ConcreteExpression obj, Context c);

            virtual ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Context c);
            virtual ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c);
            
            virtual OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c);

            virtual bool IsA(Type* other, Analyzer& a);
            virtual Type* GetConstantContext(Analyzer& a) {
                return nullptr;
            }
                                                
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
            Type* GetConstantContext(Analyzer& a) override {
                return this;
            }
        };
        template<typename F, typename T> Callable* make_assignment_callable(F f, T* self,  Context c) {            
            struct assign : public Callable, public MetaType {
                T* self;
                F action;
                assign(T* obj, F func)
                    : self(obj), action(std::move(func)) {}

                ConcreteExpression BuildCall(ConcreteExpression lhs, std::vector<ConcreteExpression> args, Context c) override {
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
                ConcreteExpression BuildCall(ConcreteExpression lhs, std::vector<ConcreteExpression> args, Context c) override {
                    // Overload resolution should not pick us unless the args are a fit. Assert if we are picked and it's not correct.
                    assert(args.size() == 2);
                    // It's now possible for us to have U instead of T. If the type is not T, build one from the argument.
                    if (args[0].t->Decay() != self)
                        args[0] = self->BuildValueConstruction(args[0], c);
                    if (args[1].t->Decay() != self)
                        args[1] = self->BuildValueConstruction(args[1], c);
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
