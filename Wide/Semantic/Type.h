#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Optional.h>
#include <boost/variant.hpp>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
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

namespace std {
    template<> struct hash<Wide::Lexer::Access> {
        std::size_t operator()(Wide::Lexer::Access a) const {
            return std::hash<int>()(int(a));
        }
    };
}

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
            Context(Context&& other)
                : where(other.where), a(other.a), source(other.source), RAIIHandler(other.RAIIHandler) {}
            
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

        struct Node {
            std::unordered_set<Node*> listeners;
            std::unordered_set<Node*> listening_to;
            void AddChangedListener(Node* n) { listeners.insert(n); }
            void RemoveChangedListener(Node* n) { listeners.erase(n); }
        protected:
            virtual void OnNodeChanged(Node* n) {}
            void ListenToNode(Node* n) {
                n->AddChangedListener(this);
                listening_to.insert(n);
            }
            void StopListeningToNode(Node* n) {
                n->RemoveChangedListener(this);
                listening_to.erase(n);
            }
            void OnChange() {
                for (auto node : listeners)
                    node->OnNodeChanged(this);
            }
        public:
            virtual ~Node() {
                for (auto node : listening_to)
                    node->RemoveChangedListener(this);
            }
        };
        struct Expression : public Node {
            virtual Type* GetType() = 0;
            virtual Codegen::Expression* GenerateCode(Codegen::Generator* g) = 0;
        };
        
        struct Type {
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
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU, Analyzer& a);

            virtual std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) = 0;
            virtual std::size_t size(Analyzer& a) = 0;
            virtual std::size_t alignment(Analyzer& a) = 0;

            virtual bool IsMoveConstructible(Analyzer& a, Lexer::Access access);
            virtual bool IsCopyConstructible(Analyzer& a, Lexer::Access access);

            virtual bool IsMoveAssignable(Analyzer& a, Lexer::Access access);
            virtual bool IsCopyAssignable(Analyzer& a, Lexer::Access access);
            
            virtual Wide::Util::optional<ConcreteExpression> AccessStaticMember(std::string name, Context c) {
                return Wide::Util::none;
            }
            virtual Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c);

            virtual ConcreteExpression BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
                throw NoMetaCall(val.t, c.where, *c);
            }
            virtual Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Context c);
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c);
            virtual ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c);

            virtual ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c);
            
            virtual bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access);
            virtual Type* GetConstantContext(Analyzer& a);

            virtual std::string explain(Analyzer& a) = 0;
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
        struct TupleInitializable {
            virtual Type* GetSelfAsType() = 0;
            virtual Wide::Util::optional<std::vector<Type*>> GetTypesForTuple(Analyzer& a) = 0;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access);
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
            virtual Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*>, Analyzer& a, Type* source) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) = 0;
        };

        enum InheritanceRelationship {
            NotDerived,
            AmbiguouslyDerived,
            UnambiguouslyDerived
        };
        struct BaseType {
        public:
            struct VirtualFunction {
                std::string name;
                std::vector<Type*> args;
                Type* ret;
                bool abstract;
            };
        private:
            std::unordered_map<std::vector<std::pair<BaseType*, unsigned>>, Codegen::Expression*, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<std::vector<VirtualFunction>> VtableLayout;
            Codegen::Expression* CreateVTable(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a);
            Codegen::Expression* GetVTablePointer(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a);
            virtual std::vector<VirtualFunction> ComputeVTableLayout(Analyzer& a) = 0;
            virtual Codegen::Expression* GetVirtualPointer(Codegen::Expression* self, Analyzer& a) = 0;
            virtual std::function<llvm::Type*(llvm::Module*)> GetVirtualPointerType(Analyzer& a) = 0;
            virtual Codegen::Expression* FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset, Analyzer& a) = 0;
            virtual std::vector<std::pair<BaseType*, unsigned>> GetBases(Analyzer& a) = 0;
            Codegen::Expression* SetVirtualPointers(std::vector<std::pair<BaseType*, unsigned>> path, Codegen::Expression* self, Analyzer& a);
        public:
            std::vector<VirtualFunction> GetVtableLayout(Analyzer& a) {
                if (!VtableLayout)
                    VtableLayout = ComputeVTableLayout(a);
                return *VtableLayout;
            }
            Codegen::Expression* SetVirtualPointers(Codegen::Expression* self, Analyzer& a);

            virtual Type* GetSelfAsType() = 0;
            virtual InheritanceRelationship IsDerivedFrom(Type* other, Analyzer& a) = 0;
            virtual Codegen::Expression* AccessBase(Type* other, Codegen::Expression*, Analyzer& a) = 0;
        };
        struct MemberFunctionContext { virtual ~MemberFunctionContext() {} };
        class PrimitiveType : public Type {
        protected:
            PrimitiveType() {}
        public:
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Lexer::Access access, Analyzer& a) override;
        };
        class MetaType : public PrimitiveType {
        public:
            MetaType() {}
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            Type* GetConstantContext(Analyzer& a) override;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override final;
        };
        class Concept {
        protected:
            virtual Type* ConstrainType(Type* t) = 0;
        public:
            virtual bool CanConstrainType(Type* t) = 0;
            Type* GetConstrainedType(Type* t) {
                if (CanConstrainType(t))
                    return ConstrainType(t);
                return nullptr;
            }
        };
        std::vector<ConcreteExpression> AdjustArgumentsForTypes(std::vector<ConcreteExpression>, std::vector<Type*>, Context c);
        OverloadResolvable* make_resolvable(std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> f, std::vector<Type*> types, Analyzer& a);
    }
}
