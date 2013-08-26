#pragma once
#include <Wide/Semantic/Type.h>
namespace Wide {
    namespace Semantic {
        class FloatType : public Type {
            unsigned bits;
        public:
            FloatType(unsigned bit) : bits(bit) {}

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;

            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            Expression BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType type, Analyzer& a) override;
        };
    }
}