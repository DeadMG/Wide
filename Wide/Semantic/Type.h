#pragma once

#include <Wide/Parser/AST.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <list>
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
            CodegenContext(const CodegenContext&) = default;
            struct EHScope {
                CodegenContext* context;
                llvm::BasicBlock* target;
                llvm::PHINode* phi;
                std::vector<llvm::Constant*> types;
            };

            operator llvm::LLVMContext&() { return module->getContext(); }
            llvm::IRBuilder<>* operator->() { return insert_builder; }
            operator llvm::Module*() { return module; }

            std::list<std::pair<std::function<void(CodegenContext&)>, bool>> GetAddedDestructors(CodegenContext& other) {
                return std::list<std::pair<std::function<void(CodegenContext&)>, bool>>(std::next(other.Destructors.begin(), Destructors.size()), other.Destructors.end());
            }
            void GenerateCodeAndDestroyLocals(std::function<void(CodegenContext&)> action);
            void DestroyDifference(CodegenContext& other, bool EH);
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

            llvm::AllocaInst* CreateAlloca(Type* t);
            llvm::Value* CreateStructGEP(llvm::Value* v, unsigned num);

            bool destructing = false;
            bool catching = false;
            llvm::Module* module;
            // Mostly used for e.g. member variables.
            Wide::Util::optional<EHScope> EHHandler;
        private:
            CodegenContext(llvm::Module* mod, llvm::IRBuilder<>& alloc_builder, llvm::IRBuilder<>& gep_builder, llvm::IRBuilder<>& ir_builder);
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>> Destructors;
            llvm::IRBuilder<>* alloca_builder;
            llvm::IRBuilder<>* insert_builder;
            llvm::IRBuilder<>* gep_builder;
            std::shared_ptr<std::unordered_map<llvm::AllocaInst*, std::unordered_map<unsigned, llvm::Value*>>> gep_map;
        public:
            bool HasDestructors();
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator AddDestructor(std::function<void(CodegenContext&)>);
            std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator AddExceptionOnlyDestructor(std::function<void(CodegenContext&)>);
            void EraseDestructor(std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator it);
            void AddDestructors(std::list<std::pair<std::function<void(CodegenContext&)>, bool>>);
            static void EmitFunctionBody(llvm::Function* func, std::function<void(CodegenContext&)> body);
        };

        struct Statement : public Node {
            virtual void GenerateCode(CodegenContext& con) = 0;
        };
        struct Expression : public Statement {
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(CodegenContext& con);
            virtual Expression* GetImplementation() { return this; }
        private:
            Wide::Util::optional<llvm::Value*> val;
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
            std::unordered_map<Parse::Access, OverloadSet*> ConstructorOverloadSets;
            std::unordered_map<Parse::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>> OperatorOverloadSets;
            std::unordered_map<Parse::Access, std::unordered_map<Lexer::TokenType, OverloadSet*>> ADLResults;
            std::unordered_map<std::vector<std::pair<Type*, unsigned>>, std::shared_ptr<Expression>, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<VTableLayout> VtableLayout;
            Wide::Util::optional<VTableLayout> PrimaryVtableLayout;

            VTableLayout ComputeVTableLayout();
            std::shared_ptr<Expression> CreateVTable(std::vector<std::pair<Type*, unsigned>> path);
            std::shared_ptr<Expression> GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path);
            static std::shared_ptr<Expression> SetVirtualPointers(std::shared_ptr<Expression> self, std::vector<std::pair<Type*, unsigned>> path);
        protected:
            llvm::Function* DestructorFunction = nullptr;
            virtual llvm::Function* CreateDestructorFunction(llvm::Module* module);
            virtual OverloadSet* CreateOperatorOverloadSet(Lexer::TokenType what, Parse::Access access);
            virtual OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Parse::Access access);
            virtual OverloadSet* CreateConstructorOverloadSet(Parse::Access access) = 0;
        public:
            virtual std::shared_ptr<Expression> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) { assert(false); throw std::runtime_error("ICE"); }
            Type(Analyzer& a) : analyzer(a) {}

            Analyzer& analyzer;

            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;
            virtual llvm::Type* GetLLVMType(llvm::Module* module) = 0;
            virtual llvm::Constant* GetRTTI(llvm::Module* module);

            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual Type* GetContext();
            virtual bool IsComplexType();
            virtual bool IsTriviallyDestructible();
            virtual bool IsTriviallyCopyConstructible();
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU);
            virtual bool IsMoveConstructible(Parse::Access access);
            virtual bool IsCopyConstructible(Parse::Access access);
            virtual bool IsMoveAssignable(Parse::Access access);
            virtual bool IsCopyAssignable(Parse::Access access);
            virtual Type* GetConstantContext();
            virtual bool IsEmpty();
            virtual Type* GetVirtualPointerType();
            virtual std::vector<Type*> GetBases();
            virtual Type* GetPrimaryBase();
            virtual std::unordered_map<unsigned, std::unordered_set<Type*>> GetEmptyLayout();
            virtual VTableLayout ComputePrimaryVTableLayout();
            virtual std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets();
            // Do not ever call from public API, it is for derived types and implementation details only.
            virtual bool IsSourceATarget(Type* source, Type* target, Type* context) { return false; }

            virtual std::shared_ptr<Expression> GetVirtualPointer(std::shared_ptr<Expression> self);
            virtual std::shared_ptr<Expression> AccessStaticMember(std::string name);
            virtual std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, std::string name, Context c);
            virtual std::shared_ptr<Expression> BuildMetaCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args);
            virtual std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c);
            virtual std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize);

            std::shared_ptr<Expression> BuildValueConstruction(std::vector<std::shared_ptr<Expression>> args, Context c);
            std::shared_ptr<Expression> BuildRvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c);
            std::shared_ptr<Expression> BuildLvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c);

            virtual ~Type() {}

            llvm::Function* GetDestructorFunction(llvm::Module* module);
            InheritanceRelationship IsDerivedFrom(Type* other);
            VTableLayout GetVtableLayout();
            VTableLayout GetPrimaryVTable();
            OverloadSet* GetConstructorOverloadSet(Parse::Access access);
            OverloadSet* PerformADL(Lexer::TokenType what, Parse::Access access);
            OverloadSet* AccessMember(Lexer::TokenType type, Parse::Access access);
            bool InheritsFromAtOffsetZero(Type* other);

            static std::shared_ptr<Expression> BuildBooleanConversion(std::shared_ptr<Expression>, Context);
            static std::shared_ptr<Expression> AccessBase(std::shared_ptr<Expression> self, Type* other);
            static std::shared_ptr<Expression> BuildInplaceConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> exprs, Context c);
            static std::shared_ptr<Expression> BuildUnaryExpression(std::shared_ptr<Expression> self, Lexer::TokenType type, Context c);
            static std::shared_ptr<Expression> BuildBinaryExpression(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Lexer::TokenType type, Context c);
            static std::shared_ptr<Expression> SetVirtualPointers(std::shared_ptr<Expression>);
            static std::shared_ptr<Expression> BuildIndex(std::shared_ptr<Expression> obj, std::shared_ptr<Expression> arg, Context c);           
            static bool IsFirstASecond(Type* first, Type* second, Type* context);
        };

        struct Callable {
        public:
            std::shared_ptr<Expression> Call(std::vector<std::shared_ptr<Expression>> args, Context c);
        private:
            virtual std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
            virtual std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
        };
        struct OverloadResolvable {
            virtual Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*>, Analyzer& a, Type* source) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) = 0;
        };

        std::vector<std::shared_ptr<Expression>> AdjustArgumentsForTypes(std::vector<std::shared_ptr<Expression>> args, std::vector<Type*> types, Context c);
        std::unique_ptr<OverloadResolvable> MakeResolvable(std::function<std::shared_ptr<Expression>(std::vector<std::shared_ptr<Expression>>, Context)> f, std::vector<Type*> types);

        struct TupleInitializable {
        private:
            std::unique_ptr<OverloadResolvable> TupleConstructor;
        public:
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access);
            virtual Type* GetSelfAsType() = 0;
            virtual Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() = 0;
            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) = 0;
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
                std::function<std::shared_ptr<Expression>(std::shared_ptr<Expression>)> InClassInitializer;
                Lexer::Range location;
            };
            virtual std::vector<member> GetConstructionMembers() = 0;
            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) = 0;
        };

        class PrimitiveType : public Type {
            std::unique_ptr<OverloadResolvable> CopyConstructor;
            std::unique_ptr<OverloadResolvable> ValueConstructor;
            std::unique_ptr<OverloadResolvable> MoveConstructor;
            std::unique_ptr<OverloadResolvable> AssignmentOperator;
        protected:
            PrimitiveType(Analyzer& a) : Type(a) {}
        public:
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Lexer::TokenType what, Parse::Access access) override;
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
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
        };
        
        llvm::Constant* GetGlobalString(std::string string, llvm::Module* m);
    }
}
