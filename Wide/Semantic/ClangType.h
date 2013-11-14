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
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;  

            Wide::Util::optional<Expression> AccessMember(ConcreteExpression val, std::string name, Context c) override;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Context c) override;

            bool IsComplexType() override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override;
            ConcreteExpression BuildDereference(ConcreteExpression obj, Context c) override;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Context c) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            Type* GetContext(Analyzer& a) override;
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c) override;
            OverloadSet* AccessMember(ConcreteExpression self, Lexer::TokenType name, Context c) override;
        };
    }
}