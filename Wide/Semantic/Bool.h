#pragma once
#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class Bool : public Type {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            clang::QualType GetClangType(ClangUtil::ClangTU&, Analyzer& a) override;

            Codegen::Expression* BuildBooleanConversion(ConcreteExpression, Analyzer& a) override;
            ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Wide::Lexer::TokenType type, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}