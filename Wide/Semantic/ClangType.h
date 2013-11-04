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

            Wide::Util::optional<Expression> AccessMember(ConcreteExpression val, std::string name, Analyzer& a, Lexer::Range where) override;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Analyzer& a, Lexer::Range where) override;

            bool IsComplexType() override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            ConcreteExpression BuildDereference(ConcreteExpression obj, Analyzer& a, Lexer::Range where) override;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a, Lexer::Range where) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            Type* GetContext(Analyzer& a) override;
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Analyzer& a, Lexer::Range where) override;
            OverloadSet* AccessMember(ConcreteExpression self, Lexer::TokenType name, Analyzer& a, Lexer::Range where) override;
        };
    }
}