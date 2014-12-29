#pragma once

#include <Wide/Parser/AST.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/Expression.h>
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
        
        class FunctionType;
        enum class OperatorAccess {
            Implicit,
            Explicit
        };
    }
}
namespace std {
    template<> struct hash<Wide::Semantic::OperatorAccess> {
        std::size_t operator()(Wide::Semantic::OperatorAccess kind) const {
            return std::hash<int>()((int)kind);
        }
    };
}
namespace Wide {
    namespace Semantic {
        struct Type {
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
                    Parse::Name name;
                    bool final;
                    bool abstract;
                };
                struct VirtualFunctionEntry {
                    boost::variant<SpecialMember, VirtualFunction> func;
                    FunctionType* type;
                };
                unsigned offset;
                std::vector<VirtualFunctionEntry> layout;
            };

        private:
            std::unordered_map<Parse::Access, OverloadSet*> ConstructorOverloadSets;
            std::unordered_map<Parse::Access, std::unordered_map<Parse::OperatorName, std::unordered_map<OperatorAccess, OverloadSet*>>> OperatorOverloadSets;
            std::unordered_map<Parse::Access, std::unordered_map<Parse::OperatorName, OverloadSet*>> ADLResults;
            std::unordered_map<std::vector<std::pair<Type*, unsigned>>, std::shared_ptr<Expression>, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<VTableLayout> VtableLayout;
            Wide::Util::optional<VTableLayout> PrimaryVtableLayout;

            VTableLayout ComputeVTableLayout();
            std::shared_ptr<Expression> CreateVTable(std::vector<std::pair<Type*, unsigned>> path);
            std::shared_ptr<Expression> GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path);
            static std::shared_ptr<Expression> SetVirtualPointers(std::shared_ptr<Expression> self, std::vector<std::pair<Type*, unsigned>> path);
            virtual VTableLayout ComputePrimaryVTableLayout();
            virtual std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c);
            virtual std::shared_ptr<Expression> AccessVirtualPointer(std::shared_ptr<Expression> self);
        protected:
            virtual std::function<void(CodegenContext&)> BuildDestruction(std::shared_ptr<Expression> self, Context c, bool devirtualize);
            virtual std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c);
            llvm::Function* DestructorFunction = nullptr;
            virtual llvm::Function* CreateDestructorFunction(llvm::Module* module);
            virtual OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess implicit);
            virtual OverloadSet* CreateADLOverloadSet(Parse::OperatorName name, Parse::Access access);
            virtual OverloadSet* CreateConstructorOverloadSet(Parse::Access access) = 0;
        public:
            virtual std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry) { assert(false); throw std::runtime_error("ICE"); }
            Type(Analyzer& a) : analyzer(a) {}

            Analyzer& analyzer;

            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;
            virtual llvm::Type* GetLLVMType(llvm::Module* module) = 0;

            virtual bool IsFinal();
            virtual bool IsAbstract();
            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual Type* GetContext();
            virtual bool AlwaysKeepInMemory(llvm::Module* mod);
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
            virtual bool HasVirtualDestructor();
            virtual std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets();
            virtual bool IsNonstaticMemberContext();
            virtual bool IsLookupContext();
            virtual std::function<llvm::Constant*(llvm::Module*)> GetRTTI();
            virtual std::string Export();
            virtual std::string GetExportBody();
            virtual void Export(llvm::Module* mod);

            // Do not ever call from public API, it is for derived types and implementation details only.
            virtual bool IsSourceATarget(Type* source, Type* target, Type* context) { return false; }
            virtual std::shared_ptr<Expression> AccessStaticMember(std::string name, Context c);

            std::shared_ptr<Expression> BuildValueConstruction(std::vector<std::shared_ptr<Expression>> args, Context c);
            std::shared_ptr<Expression> BuildRvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c);
            std::shared_ptr<Expression> BuildLvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c);

            virtual ~Type() {}

            llvm::Function* GetDestructorFunction(llvm::Module* module);
            InheritanceRelationship IsDerivedFrom(Type* other);
            VTableLayout GetVtableLayout();
            VTableLayout GetPrimaryVTable();
            OverloadSet* GetConstructorOverloadSet(Parse::Access access);
            OverloadSet* PerformADL(Parse::OperatorName what, Parse::Access access);
            
            OverloadSet* AccessMember(Parse::OperatorName type, Parse::Access access, OperatorAccess implicit);
            std::shared_ptr<Expression> AccessStaticMember(Parse::Name name, Context c);
            bool InheritsFromAtOffsetZero(Type* other);
            unsigned GetOffsetToBase(Type* base);

            std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize);
            static std::shared_ptr<Expression> GetVirtualPointer(std::shared_ptr<Expression> self);
            static std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c);
            static std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, Parse::Name name, Context c);
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
            virtual std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
            virtual std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
        };
        struct OverloadResolvable {
            virtual Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*>, Analyzer& a, Type* source) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) = 0;
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
                std::function<unsigned()> num;
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
            bool IsMoveConstructible(Parse::Access) override final { return true; }
            bool IsCopyConstructible(Parse::Access) override final { return true; }
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) override;
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
            using Type::AccessMember;
        };
        
        llvm::Constant* GetGlobalString(std::string string, llvm::Module* m);
    }
}
