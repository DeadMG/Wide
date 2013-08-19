#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public Type {
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) override;
            Expression BuildDereference(Expression obj, Analyzer& a) override;
			Expression BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType type, Analyzer& a);
            Codegen::Expression* BuildBooleanConversion(Expression val, Analyzer& a) override;
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}
