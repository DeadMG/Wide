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
        class ClangType : public Type, public TupleInitializable, public BaseType {
            ClangTU* from;
            clang::QualType type; 
            void ProcessImplicitSpecialMember(std::function<bool()> needs, std::function<clang::CXXMethodDecl*()> declare, std::function<void(clang::CXXMethodDecl*)> define, std::function<clang::CXXMethodDecl*()> lookup);
            bool ProcessedConstructors = false;
            bool ProcessedDestructors = false;
            bool ProcessedAssignmentOperators = false;
            Type* GetSelfAsType() override final { return this; }
            std::vector<std::pair<BaseType*, unsigned>> GetBases() override final;
            Type* GetVirtualPointerType() override final;
            std::vector<VirtualFunction> ComputeVTableLayout() override final;
            std::unique_ptr<Expression> FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset) override final;
        public:
            std::unique_ptr<Expression> GetVirtualPointer(std::unique_ptr<Expression>) override final;
            ClangType(ClangTU* src, clang::QualType t, Analyzer& a);
            llvm::Type* GetLLVMType(Codegen::Generator& a) override final;            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) override final;        
            std::unique_ptr<Expression> BuildBooleanConversion(std::unique_ptr<Expression> ex, Context c) override final; 
            std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c) override final;
            
            bool IsEliminateType() override final;
            bool IsComplexType(Codegen::Generator&) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetContext() override final;
            OverloadSet* CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) override final;
            InheritanceRelationship IsDerivedFrom(Type* other) override final;
            std::unique_ptr<Expression> AccessBase(std::unique_ptr<Expression> self, Type* other) override final;
            std::string explain() override final;
        };
    }
}