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
    class Value;
}
namespace clang {
    class QualType;
}
namespace Wide {
    namespace Codegen {
        class Generator;
    }
    namespace Lexer {
        enum class TokenType : int;
    }
    namespace Semantic {
        class ClangTU;
        class Analyzer;
        class OverloadSet;
        class Error;
        struct Type;

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
        struct Statement : public Node {
            virtual void GenerateCode(Codegen::Generator& g) = 0;
            virtual void DestroyLocals(Codegen::Generator& g) = 0;
        };
        struct Expression : public Statement {
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(Codegen::Generator& g) {
                if (!val) val = ComputeValue(g);
                return val;
            }
        private:
            llvm::Value* val = nullptr;
            void GenerateCode(Codegen::Generator& g) override final {
                GetValue(g);
            }
            virtual llvm::Value* ComputeValue(Codegen::Generator& g) = 0;
        };

        
        struct Type : public Node {
            std::unordered_map<Lexer::Access, OverloadSet*> ConstructorOverloadSet;
            std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>> OperatorOverloadSets;
            std::unordered_map<Type*, std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>>> ADLResults;

            virtual OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access);
            virtual OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) = 0;
            virtual OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access);
        protected:
            Analyzer& analyzer;
        public:
            Type(Analyzer& a) : analyzer(a) {}

            virtual llvm::Type* GetLLVMType(Codegen::Generator& g) = 0;
            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;

            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual Type* GetContext();
            virtual bool IsComplexType() { return false; }
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU);
            virtual bool IsMoveConstructible(Lexer::Access access);
            virtual bool IsCopyConstructible(Lexer::Access access);
            virtual bool IsMoveAssignable(Lexer::Access access);
            virtual bool IsCopyAssignable(Lexer::Access access);
            virtual bool IsA(Type* self, Type* other, Lexer::Access access);
            virtual Type* GetConstantContext();

            virtual std::unique_ptr<Expression> AccessStaticMember(std::string name);
            virtual std::unique_ptr<Expression> AccessMember(Expression* t, std::string name, Lexer::Access);
            virtual std::unique_ptr<Expression> BuildValueConstruction(std::vector<Expression*> types);
            virtual std::unique_ptr<Expression> BuildMetaCall(Expression* val, std::vector<Expression*> args);
            virtual std::unique_ptr<Expression> BuildCall(Expression* val, std::vector<Expression*> args);

            virtual std::function<llvm::Value*(llvm::Value*, Codegen::Generator&)> BuildBooleanConversion(Type* t);
            virtual std::function<void(llvm::Value*, Codegen::Generator&)> BuildDestructorCall();

            virtual ~Type();

            OverloadSet* GetConstructorOverloadSet(Lexer::Access access);
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access);
            OverloadSet* AccessMember(Type* t, Lexer::TokenType type, Lexer::Access access);

            std::unique_ptr<Expression> BuildInplaceConstruction(std::function<llvm::Value*()>, std::vector<Expression*> exprs);
            std::unique_ptr<Expression> BuildRvalueConstruction(std::vector<Expression*> exprs);
            std::unique_ptr<Expression> BuildLvalueConstruction(std::vector<Expression*> exprs);
            std::unique_ptr<Expression> BuildUnaryExpression(Expression* self, Lexer::TokenType type);
            std::unique_ptr<Expression> BuildBinaryExpression(Expression* lhs, Expression* rhs, Lexer::TokenType type);
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
            virtual std::vector<VirtualFunction> ComputeVTableLayout(Analyzer& a) = 0;
            virtual Codegen::Expression* GetVirtualPointer(Codegen::Expression* self, Analyzer& a) = 0;
            virtual std::function<llvm::Type*(llvm::Module*)> GetVirtualPointerType(Analyzer& a) = 0;
            virtual Codegen::Expression* FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset, Analyzer& a) = 0;
            virtual std::vector<std::pair<BaseType*, unsigned>> GetBases(Analyzer& a) = 0;

            std::unordered_map<std::vector<std::pair<BaseType*, unsigned>>, Codegen::Expression*, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<std::vector<VirtualFunction>> VtableLayout;
            Codegen::Expression* CreateVTable(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a);
            Codegen::Expression* GetVTablePointer(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a);
            Codegen::Expression* SetVirtualPointers(std::vector<std::pair<BaseType*, unsigned>> path, Codegen::Expression* self, Analyzer& a);
        public:
            std::vector<VirtualFunction> GetVtableLayout(Analyzer& a) {
                if (!VtableLayout)
                    VtableLayout = ComputeVTableLayout(a);
                return *VtableLayout;
            }
            std::function<void(llvm::Value*, Codegen::Generator&)> SetVirtualPointers();

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
            llvm::Type* GetLLVMType(Codegen::Generator& g) {}
            std::size_t size() override;
            std::size_t alignment() override;
            Type* GetConstantContext(Analyzer& a) override;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override final;
        };
        std::vector<ConcreteExpression> AdjustArgumentsForTypes(std::vector<ConcreteExpression>, std::vector<Type*>, Context c);
        OverloadResolvable* make_resolvable(std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> f, std::vector<Type*> types, Analyzer& a);
    }
}
