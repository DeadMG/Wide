#pragma once

#include <Wide/Semantic/Type.h>
#include <functional>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)

namespace clang {
    class CXXMethodDecl;
    class LookupResult;
}
namespace Wide {
    namespace Semantic {     
        class ClangTU;
        class ClangType : public Type, public TupleInitializable, public ConstructorContext {
            Location l;
            ClangTU* from;
            clang::QualType type; 
            void ProcessImplicitSpecialMember(std::function<bool()> needs, std::function<clang::CXXMethodDecl*()> declare, std::function<void(clang::CXXMethodDecl*)> define, std::function<clang::CXXMethodDecl*()> lookup);
            
            bool ProcessedConstructors = false;
            bool ProcessedDestructors = false;
            bool ProcessedAssignmentOperators = false;
            Wide::Util::optional<std::vector<Type*>> Bases;

            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> boollvalue;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> boolrvalue;

            Type* GetSelfAsType() override final { return this; }
            std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets() override final;
            Type* GetVirtualPointerType() override final;
            VTableLayout ComputePrimaryVTableLayout() override final;
            std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> VirtualEntryFor(VTableLayout::VirtualFunctionEntry) override final;
            std::vector<member> GetConstructionMembers() override final;
            bool HasVirtualDestructor() override final;
            bool IsNonstaticMemberContext() override final { return true; }
            bool AlwaysKeepInMemory(llvm::Module* mod) override final;
        public:
            // Used for type.destructor access.
            std::function<llvm::Constant*(llvm::Module*)> GetRTTI() override final;
            OverloadSet* GetDestructorOverloadSet();
            std::shared_ptr<Expression> AccessVirtualPointer(Expression::InstanceKey key, std::shared_ptr<Expression>) override final;
            ClangType(ClangTU* src, clang::QualType t, Analyzer& a);
            llvm::Type* GetLLVMType(llvm::Module* m) override final;            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::function<void(CodegenContext&)> BuildDestruction(Expression::InstanceKey, std::shared_ptr<Expression> self, Context c, bool devirtualize) override final;
            bool IsConstant() override final; 

            bool IsSourceATarget(Type* first, Type* second, Location context) override final;
            bool IsEmpty() override final;
            bool IsTriviallyDestructible() override final;
            bool IsTriviallyCopyConstructible() override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            OverloadSet* CreateADLOverloadSet(Parse::OperatorName what, Location from) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName name, Parse::Access access, OperatorAccess kind) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) override final;
            std::string explain() override final; 
            std::shared_ptr<Expression> AccessStaticMember(std::string name, Context c) override final;
            bool IsFinal() override final;
        };
    }
}