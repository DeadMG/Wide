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
            std::vector<std::pair<BaseType*, unsigned>> GetBases(Analyzer& a) override final;
            Codegen::Expression* GetVirtualPointer(Codegen::Expression* self, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetVirtualPointerType(Analyzer&) override final;
            std::vector<VirtualFunction> ComputeVTableLayout(Analyzer& a) override final;
            Codegen::Expression* FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset, Analyzer& a)override final;
        public:
            ClangType(ClangTU* src, clang::QualType t);         
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu, Analyzer& a) override final;

            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Context c) override final;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Context c) override final;

            bool IsComplexType(Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            Type* GetContext(Analyzer& a) override final;
            OverloadSet* CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access, Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&, Lexer::Access) override final;
            OverloadSet* CreateDestructorOverloadSet(Wide::Semantic::Analyzer&) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple(Analyzer& a) override final;
            ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) override final;
            InheritanceRelationship IsDerivedFrom(Type* other, Analyzer& a) override final;
            Codegen::Expression* AccessBase(Type* other, Codegen::Expression*, Analyzer& a) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}