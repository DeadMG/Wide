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
        class ClangType : public Type {
            ClangUtil::ClangTU* from;
            clang::QualType type;
        public:
            ClangType(ClangUtil::ClangTU* src, clang::QualType t);         
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;            
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override final;

            Wide::Util::optional<Expression> AccessMember(ConcreteExpression val, std::string name, Context c) override final;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Context c) override final;

            bool IsComplexType() override final;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override final;
            ConcreteExpression BuildDereference(ConcreteExpression obj, Context c) override final;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Context c) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            Type* GetContext(Analyzer& a) override final;
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c) override final;
            OverloadSet* AccessMember(ConcreteExpression self, Lexer::TokenType name, Context c) override final;
            bool IsCopyable(Analyzer& a) override final;
            bool IsMovable(Analyzer& a) override final;
        };
    }
}