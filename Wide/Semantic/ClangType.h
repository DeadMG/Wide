#pragma once

#include <Wide/Semantic/Type.h>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)

namespace clang {
    class LookupResult;
}
namespace Wide {
    namespace Semantic {     
        class ClangTU;
        class ClangType : public virtual Type, public TupleInitializable, public BaseType {
            ClangTU* from;
            clang::QualType type;
        public:
            ClangType(ClangTU* src, clang::QualType t);         
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;            
            clang::QualType GetClangType(ClangTU& tu, Analyzer& a) override final;

            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Context c) override final;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Context c) override final;

            bool IsComplexType(Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            Type* GetContext(Analyzer& a) override final;
            OverloadSet* CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Analyzer& a) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&) override final;
            OverloadSet* CreateDestructorOverloadSet(Wide::Semantic::Analyzer&) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple(Analyzer& a) override final;
            ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) override final;
            InheritanceRelationship IsDerivedFrom(Type* other, Analyzer& a) override final;
            Codegen::Expression* AccessBase(Type* other, Codegen::Expression*, Analyzer& a) override final;
        };
    }
}