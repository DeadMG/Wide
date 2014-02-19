#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Optional.h>
#include <boost/variant.hpp>
#include <vector>
#include <stdexcept>
#include <unordered_map>
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
        class ClangTU;
        class Analyzer;
        class OverloadSet;
        struct Type;
        struct DeferredExpression;
        struct ConcreteExpression;
        struct Context {
            Context(const Context& other) = default;
            Context(Context&& other) = default;

            //Context(Analyzer* an, Lexer::Range loc, std::function<void(ConcreteExpression)> handler, Type* t)
            //    : a(an), where(loc), RAIIHandler(std::move(handler)), source(t) {}

            Context(Analyzer& an, Lexer::Range loc, std::function<void(ConcreteExpression)> handler, Type* s)
                : a(&an), where(loc), RAIIHandler(std::move(handler)), source(s) {}

            Analyzer* a;
            Analyzer* operator->() const { return a; }
            Analyzer& operator*() const { return *a; }

            Lexer::Range where;
            void operator()(ConcreteExpression e);
            std::function<void(ConcreteExpression)> RAIIHandler;
            Type* source;
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
            std::unordered_map<Lexer::Access, OverloadSet*> ConstructorOverloadSet;
            std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>> OperatorOverloadSets;
            OverloadSet* DestructorOverloadSet;
            std::unordered_map<Type*, std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>>> ADLResults;

            virtual OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access, Analyzer& a);
            virtual OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) = 0;
            virtual OverloadSet* CreateDestructorOverloadSet(Analyzer& a);
            virtual OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a);
        public:
            Type() : DestructorOverloadSet(nullptr) {}

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

            virtual bool IsComplexType(Analyzer& a) { return false; }
            virtual clang::QualType GetClangType(ClangTU& TU, Analyzer& a);
            virtual std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) {
                throw std::runtime_error("This type has no LLVM counterpart.");
            }

            virtual bool IsMoveConstructible(Analyzer& a, Lexer::Access access);
            virtual bool IsCopyConstructible(Analyzer& a, Lexer::Access access);

            virtual bool IsMoveAssignable(Analyzer& a, Lexer::Access access);
            virtual bool IsCopyAssignable(Analyzer& a, Lexer::Access access);

            virtual std::size_t size(Analyzer& a) { throw std::runtime_error("Attempted to size a type that does not have a run-time size."); }
            virtual std::size_t alignment(Analyzer& a) { throw std::runtime_error("Attempted to align a type that does not have a run-time alignment."); }   
            
            virtual Wide::Util::optional<ConcreteExpression> AccessStaticMember(std::string name, Context c) {
                throw std::runtime_error("This type does not have any static members.");
            }
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c);

            virtual ConcreteExpression BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Context c) {
                if (IsReference())
                    return Decay()->BuildBooleanConversion(val, c);
                throw std::runtime_error("Could not convert a type to boolean.");
            }
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c);
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c);

            virtual ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c);


            virtual bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access);
            virtual Type* GetConstantContext(Analyzer& a);

            virtual ~Type() {}

            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a);
            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c);
            OverloadSet* AccessMember(Type* t, Lexer::TokenType type, Lexer::Access access, Analyzer& a);
            ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Context c);
            ConcreteExpression BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c);
            ConcreteExpression BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c);

            ConcreteExpression BuildUnaryExpression(ConcreteExpression self, Lexer::TokenType type, Context c);
            ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Context c);

            OverloadSet* GetConstructorOverloadSet(Analyzer& a, Lexer::Access access);
            OverloadSet* GetDestructorOverloadSet(Analyzer& a);
        };
        struct TupleInitializable : public virtual Type {
            virtual Wide::Util::optional<std::vector<Type*>> GetTypesForTuple(Analyzer& a) = 0;
            virtual OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override;
            virtual ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) = 0;
        };

        struct Callable {
        public:
            ConcreteExpression Call(std::vector<ConcreteExpression> args, Context c);
        private:
            virtual ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) = 0;
            virtual std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) = 0;
        };
        struct OverloadResolvable {
            virtual unsigned GetArgumentCount() = 0;
            virtual Type* MatchParameter(Type*, unsigned, Analyzer& a, Type* source) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) = 0;
        };

        enum InheritanceRelationship {
            NotDerived,
            AmbiguouslyDerived,
            UnambiguouslyDerived
        };
        struct BaseType : public virtual Type {
            virtual InheritanceRelationship IsDerivedFrom(Type* other, Analyzer& a) = 0;
            virtual Codegen::Expression* AccessBase(Type* other, Codegen::Expression*, Analyzer& a) = 0;
        };
        struct MemberFunctionContext { virtual ~MemberFunctionContext() {} };
        class PrimitiveType : public virtual Type {
        protected:
            PrimitiveType() {}
        public:
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Lexer::Access access, Analyzer& a) override;
        };
        class MetaType : public PrimitiveType {
        public:
            MetaType() {}
            using Type::BuildValueConstruction;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            Type* GetConstantContext(Analyzer& a) override;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override final;
        };
        std::vector<ConcreteExpression> AdjustArgumentsForTypes(std::vector<ConcreteExpression>, std::vector<Type*>, Context c);
        OverloadResolvable* make_resolvable(std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> f, std::vector<Type*> types, Analyzer& a);
    }
}
