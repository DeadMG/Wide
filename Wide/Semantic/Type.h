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
#include <boost/variant.hpp>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
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
        private:
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
                // ENABLE TO DEBUG ACCIDENTALLY DESTROYED EXPRESSIONS
                //    node->OnNodeChanged(this, Change::Destroyed);
                    node->listening_to.erase(this);
                }
            }
        };

        struct Expression;
        struct CodegenContext {
            struct EHScope {
                CodegenContext* context;
                llvm::BasicBlock* target;
                llvm::PHINode* phi;
                std::vector<llvm::Constant*> types;
            };

            operator llvm::LLVMContext&() { return module->getContext(); }
            llvm::IRBuilder<>* operator->() { return insert_builder; }
            operator llvm::Module*() { return module; }

            std::vector<Expression*> GetAddedDestructors(CodegenContext& other) {
                return std::vector<Expression*>(other.Destructors.begin() + Destructors.size(), other.Destructors.end());
            }
            void GenerateCodeAndDestroyLocals(std::function<void(CodegenContext&)> action);
            void DestroyDifference(CodegenContext& other);
            void DestroyAll(bool EH);
            void DestroyTillLastTry();
            bool IsTerminated(llvm::BasicBlock* bb);

            llvm::Function* GetEHPersonality();
            llvm::BasicBlock* GetUnreachableBlock();
            llvm::Type* GetLpadType();
            llvm::Function* GetCXABeginCatch();
            llvm::Function* GetCXAEndCatch();
            llvm::Function* GetCXARethrow();

            llvm::BasicBlock* CreateLandingpadForEH();

            llvm::PointerType* GetInt8PtrTy();
            bool destructing = false;
            bool catching = false;
            llvm::Module* module;
            llvm::IRBuilder<>* insert_builder;
            llvm::IRBuilder<>* alloca_builder;
            // Mostly used for e.g. member variables.
            std::unordered_set<Expression*> ExceptionOnlyDestructors;
            std::vector<Expression*> Destructors;
            Wide::Util::optional<EHScope> EHHandler;
        };

        struct Statement : public Node {
            virtual void GenerateCode(CodegenContext& con) = 0;
        };
        struct Expression : public Statement {
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(CodegenContext& con);
            virtual Expression* GetImplementation() { return this; }
            void DestroyLocals(CodegenContext& con) {
                assert(val);
                DestroyExpressionLocals(con);
            }
        private:
            llvm::Value* val = nullptr;
            virtual void DestroyExpressionLocals(CodegenContext& con) {}
            void GenerateCode(CodegenContext& con) override final {
                GetValue(con);
            }
            virtual llvm::Value* ComputeValue(CodegenContext& con) = 0;
        };

        struct Context {
            Context(Type* f, Lexer::Range r) : from(f), where(r) {}
            Type* from;
            Lexer::Range where;
        };
        
        struct Type : public Node {
        public:
            enum class InheritanceRelationship {
                NotDerived,
                AmbiguouslyDerived,
                UnambiguouslyDerived
            };
            struct VTableLayout {
                VTableLayout() : offset(0) {}
                enum class SpecialMember {
                    Destructor,
                    ItaniumABIDeletingDestructor,
                    OffsetToTop,
                    RTTIPointer
                };
                struct VirtualFunction {
                    std::string name;
                    std::vector<Type*> args;
                    Type* ret;
                };
                struct VirtualFunctionEntry {
                    bool abstract;
                    boost::variant<SpecialMember, VirtualFunction> function;
                };
                unsigned offset;
                std::vector<VirtualFunctionEntry> layout;
            };

        private:
            std::unordered_map<Lexer::Access, OverloadSet*> ConstructorOverloadSets;
            std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>> OperatorOverloadSets;
            std::unordered_map<Type*, std::unordered_map<Type*, std::unordered_map<Lexer::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>>>> ADLResults;
            std::unordered_map<std::vector<std::pair<Type*, unsigned>>, std::unique_ptr<Expression>, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<VTableLayout> VtableLayout;
            Wide::Util::optional<VTableLayout> PrimaryVtableLayout;
            llvm::Function* DestructorFunction = nullptr;

            VTableLayout ComputeVTableLayout();
            std::unique_ptr<Expression> CreateVTable(std::vector<std::pair<Type*, unsigned>> path);
            std::unique_ptr<Expression> GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path);
            std::unique_ptr<Expression> SetVirtualPointers(std::vector<std::pair<Type*, unsigned>> path, std::unique_ptr<Expression> self);
        protected:
            virtual OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access);
            virtual OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access);
            virtual OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) = 0;
        public:
            virtual std::unique_ptr<Expression> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) { assert(false); throw std::runtime_error("ICE"); }
            Type(Analyzer& a) : analyzer(a) {}

            Analyzer& analyzer;

            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;
            virtual llvm::Type* GetLLVMType(llvm::Module* module) = 0;
            virtual llvm::Constant* GetRTTI(llvm::Module* module);
            llvm::Value* GetDestructorFunction(llvm::Module* module);

            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual Type* GetContext();
            virtual bool IsComplexType(llvm::Module* module);
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU);
            virtual bool IsMoveConstructible(Lexer::Access access);
            virtual bool IsCopyConstructible(Lexer::Access access);
            virtual bool IsMoveAssignable(Lexer::Access access);
            virtual bool IsCopyAssignable(Lexer::Access access);
            virtual bool IsA(Type* self, Type* other, Lexer::Access access);
            virtual Type* GetConstantContext();
            virtual bool IsEmpty();
            virtual Type* GetVirtualPointerType();
            virtual std::vector<Type*> GetBases();
            virtual Type* GetPrimaryBase();
            virtual std::unordered_map<unsigned, std::unordered_set<Type*>> GetEmptyLayout();
            virtual VTableLayout ComputePrimaryVTableLayout();
            virtual std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets();

            virtual std::unique_ptr<Expression> GetVirtualPointer(std::unique_ptr<Expression> self);
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

            std::unique_ptr<Expression> AccessBase(std::unique_ptr<Expression> self, Type* other);
            InheritanceRelationship IsDerivedFrom(Type* other);
            VTableLayout GetVtableLayout();
            VTableLayout GetPrimaryVTable();
            OverloadSet* GetConstructorOverloadSet(Lexer::Access access);
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access);
            OverloadSet* AccessMember(Type* t, Lexer::TokenType type, Lexer::Access access);
            bool InheritsFromAtOffsetZero(Type* other);

            std::unique_ptr<Expression> BuildInplaceConstruction(std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> exprs, Context c);
            std::unique_ptr<Expression> BuildUnaryExpression(std::unique_ptr<Expression> self, Lexer::TokenType type, Context c);
            std::unique_ptr<Expression> BuildBinaryExpression(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Lexer::TokenType type, Context c);
            std::unique_ptr<Expression> SetVirtualPointers(std::unique_ptr<Expression>);
            std::unique_ptr<Expression> BuildIndex(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> arg, Context c);
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
        

        struct LLVMFieldIndex { unsigned index; };
        struct EmptyBaseOffset { unsigned offset; };
        typedef boost::variant<LLVMFieldIndex, EmptyBaseOffset> MemberLocation;

        struct MemberFunctionContext { virtual ~MemberFunctionContext() {} };
        struct ConstructorContext {
            struct member {
                member(Lexer::Range where)
                : location(where) {}
                member(const member&) = delete;
                member(member&& other)
                    : name(std::move(other.name))
                    , t(other.t)
                    , num(other.num)
                    , InClassInitializer(std::move(other.InClassInitializer))
                    , location(std::move(other.location)) {}
                Wide::Util::optional<std::string> name;
                Type* t;
                // Not necessarily actually an empty base but ClangType will only give us offsets pre-codegen.
                // and it could be an empty base so use offsets
                EmptyBaseOffset num;
                std::function<std::unique_ptr<Expression>(std::unique_ptr<Expression>)> InClassInitializer;
                Lexer::Range location;
            };
            virtual std::vector<member> GetConstructionMembers() = 0;
        };

        class PrimitiveType : public Type {
            std::unique_ptr<OverloadResolvable> CopyConstructor;
            std::unique_ptr<OverloadResolvable> ValueConstructor;
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
            llvm::Type* GetLLVMType(llvm::Module* module) override;
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

        llvm::Constant* GetGlobalString(std::string string, llvm::Module* m);
    }
}
