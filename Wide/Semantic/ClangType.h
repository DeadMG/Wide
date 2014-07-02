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
        class ClangType : public Type, public TupleInitializable, public MemberFunctionContext, public ConstructorContext {
            ClangTU* from;
            clang::QualType type; 
            void ProcessImplicitSpecialMember(std::function<bool()> needs, std::function<clang::CXXMethodDecl*()> declare, std::function<void(clang::CXXMethodDecl*)> define, std::function<clang::CXXMethodDecl*()> lookup);
            
            bool ProcessedConstructors = false;
            bool ProcessedDestructors = false;
            bool ProcessedAssignmentOperators = false;
            Wide::Util::optional<std::vector<Type*>> Bases;

            Type* GetSelfAsType() override final { return this; }
            std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets() override final;
            Type* GetVirtualPointerType() override final;
            VTableLayout ComputePrimaryVTableLayout() override final;
            std::shared_ptr<Expression> VirtualEntryFor(VTableLayout::VirtualFunctionEntry, unsigned offset) override final;
            std::vector<member> GetConstructionMembers() override final;
        public:
            // Used for type.destructor access.
            llvm::Constant* GetRTTI(llvm::Module* module) override final;
            OverloadSet* GetDestructorOverloadSet();
            std::shared_ptr<Expression> GetVirtualPointer(std::shared_ptr<Expression>) override final;
            ClangType(ClangTU* src, clang::QualType t, Analyzer& a);
            llvm::Type* GetLLVMType(llvm::Module* m) override final;            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;        
            std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) override final;
            Type* GetConstantContext() override final; 

            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            bool IsEmpty() override final;
            bool IsTriviallyDestructible() override final;
            bool IsTriviallyCopyConstructible() override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetContext() override final;
            OverloadSet* CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Lexer::TokenType name, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) override final;
            std::string explain() override final;
        };
    }
}