#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <memory>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/APInt.h>
#pragma warning(pop)


#pragma warning(disable : 4250)
// Decorated name length exceeded- name truncated
#pragma warning(disable : 4503)
// Performance warning for implicitly integers or pointers to bool
#pragma warning(disable : 4800)

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

        enum Change {
            Contents,
            Destroyed
        };
        struct Node {
            std::unordered_set<Node*> listeners;
            std::unordered_set<Node*> listening_to;
            void AddChangedListener(Node* n) { listeners.insert(n); }
            void RemoveChangedListener(Node* n) { listeners.erase(n); }
        protected:
            virtual void OnNodeChanged(Node* n, Change what) {}
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
                    node->OnNodeChanged(this, Change::Contents);
            }
        public:
            virtual ~Node() {
                for (auto node : listening_to)
                    node->listeners.erase(this);
                for (auto node : listeners) {
                //    node->OnNodeChanged(this, Change::Destroyed);
                    node->listening_to.erase(this);
                }
            }
        };
        struct Statement : public Node {
            virtual void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) = 0;
            virtual void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) = 0;
        };
        struct Expression : public Statement {
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(Codegen::Generator& g, llvm::IRBuilder<>& bb);
            virtual Expression* GetImplementation() { return this; }
            void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                assert(val);
                DestroyExpressionLocals(g, bb);
            }
        private:
            llvm::Value* val = nullptr;
            virtual void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) = 0;
            void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                GetValue(g, bb);
            }
            virtual llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) = 0;
        };

        struct Context {
            Context(Type* f, Lexer::Range r) : from(f), where(r) {}
            Type* from;
            Lexer::Range where;
        };
        
        struct Type : public Node {
            std::unordered_map<Lexer::Access, OverloadSet*> ConstructorOverloadSets;
            std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>> OperatorOverloadSets;
            std::unordered_map<Type*, std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>>> ADLResults;

            virtual OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access);
            virtual OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) = 0;
            virtual OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access);
        public:
            Type(Analyzer& a) : analyzer(a) {}

            Analyzer& analyzer;

            virtual llvm::Type* GetLLVMType(Codegen::Generator& g) = 0;
            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;

            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual Type* GetContext();
            virtual bool IsComplexType(Codegen::Generator& g);
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU);
            virtual bool IsMoveConstructible(Lexer::Access access);
            virtual bool IsCopyConstructible(Lexer::Access access);
            virtual bool IsMoveAssignable(Lexer::Access access);
            virtual bool IsCopyAssignable(Lexer::Access access);
            virtual bool IsA(Type* self, Type* other, Lexer::Access access);
            virtual Type* GetConstantContext();

            virtual std::unique_ptr<Expression> AccessStaticMember(std::string name);
            virtual std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c);
            virtual std::unique_ptr<Expression> BuildMetaCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args);
            virtual std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c);
            virtual std::unique_ptr<Expression> BuildValueConstruction(std::vector<std::unique_ptr<Expression>> args, Context c);

            virtual std::unique_ptr<Expression> BuildBooleanConversion(std::unique_ptr<Expression>, Context);
            virtual std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c);;

            virtual std::unique_ptr<Expression> BuildRvalueConstruction(std::vector<std::unique_ptr<Expression>> exprs, Context c);
            virtual std::unique_ptr<Expression> BuildLvalueConstruction(std::vector<std::unique_ptr<Expression>> exprs, Context c);

            virtual ~Type() {}

            OverloadSet* GetConstructorOverloadSet(Lexer::Access access);
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access);
            OverloadSet* AccessMember(Type* t, Lexer::TokenType type, Lexer::Access access);

            std::unique_ptr<Expression> BuildInplaceConstruction(std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> exprs, Context c);
            std::unique_ptr<Expression> BuildUnaryExpression(std::unique_ptr<Expression> self, Lexer::TokenType type, Context c);
            std::unique_ptr<Expression> BuildBinaryExpression(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Lexer::TokenType type, Context c);
        };


        struct Callable {
        public:
            std::unique_ptr<Expression> Call(std::vector<std::unique_ptr<Expression>> args, Context c);
        private:
            virtual std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) = 0;
            virtual std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) = 0;
        };
        struct OverloadResolvable {
            virtual Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*>, Analyzer& a, Type* source) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) = 0;
        };

        std::vector<std::unique_ptr<Expression>> AdjustArgumentsForTypes(std::vector<std::unique_ptr<Expression>> args, std::vector<Type*> types, Context c);
        std::unique_ptr<OverloadResolvable> MakeResolvable(std::function<std::unique_ptr<Expression>(std::vector<std::unique_ptr<Expression>>, Context)> f, std::vector<Type*> types);

        struct TupleInitializable {
        private:
            std::unique_ptr<OverloadResolvable> TupleConstructor;
        public:
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access);
            virtual Type* GetSelfAsType() = 0;
            virtual Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() = 0;
            virtual std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) = 0;
        };


        struct MemberFunctionContext { virtual ~MemberFunctionContext() {} };

        class PrimitiveType : public Type {
            std::unique_ptr<OverloadResolvable> CopyConstructor;
            std::unique_ptr<OverloadResolvable> MoveConstructor;
            std::unique_ptr<OverloadResolvable> AssignmentOperator;
        protected:
            PrimitiveType(Analyzer& a) : Type(a) {}
        public:
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Lexer::Access access) override;
        };
        class MetaType : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> DefaultConstructor;
        public:
            MetaType(Analyzer& a) : PrimitiveType(a) {}

            // NullType is an annoying special case needing to override these members for Reasons
            // nobody else should.
            llvm::Type* GetLLVMType(Codegen::Generator& g) override;
            std::size_t size() override;
            std::size_t alignment() override;
            Type* GetConstantContext() override;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
        };


        // Fuck you, C++ fail. This is basically just { args } but it doesn't fail super hard for move-only types
        template<typename... Args> std::vector<std::unique_ptr<Expression>> Expressions(Args&&... args) {
            using swallow = int[];
            std::vector<std::unique_ptr<Expression>> ret;
            swallow x = { 0, (ret.push_back(std::forward<Args>(args)), void(), 0)... };
            return std::move(ret);
        }

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
            virtual std::vector<VirtualFunction> ComputeVTableLayout() = 0;
            virtual Type* GetVirtualPointerType() = 0;
            virtual std::unique_ptr<Expression> FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset) = 0;
            virtual std::vector<std::pair<BaseType*, unsigned>> GetBases() = 0;

            std::unordered_map<std::vector<std::pair<BaseType*, unsigned>>, std::unique_ptr<Expression>, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<std::vector<VirtualFunction>> VtableLayout;

            std::unique_ptr<Expression> CreateVTable(std::vector<std::pair<BaseType*, unsigned>> path);
            std::unique_ptr<Expression> GetVTablePointer(std::vector<std::pair<BaseType*, unsigned>> path);
            std::unique_ptr<Expression> SetVirtualPointers(std::vector<std::pair<BaseType*, unsigned>> path, std::unique_ptr<Expression> self);
        public:
            std::vector<VirtualFunction> GetVtableLayout() {
                if (!VtableLayout)
                    VtableLayout = ComputeVTableLayout();
                return *VtableLayout;
            }
            std::unique_ptr<Expression> SetVirtualPointers(std::unique_ptr<Expression>);

            virtual std::unique_ptr<Expression> GetVirtualPointer(std::unique_ptr<Expression> self) = 0;
            virtual Type* GetSelfAsType() = 0;
            virtual InheritanceRelationship IsDerivedFrom(Type* other) = 0;
            virtual std::unique_ptr<Expression> AccessBase(std::unique_ptr<Expression> self, Type* other) = 0;
        };
    }
}
