#pragma once

#include "PrimitiveType.h"

namespace Wide {
    namespace Semantic {
        class Bool : public PrimitiveType {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU&, Analyzer& a);

            Codegen::Expression* BuildBooleanConversion(Expression, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
            Expression BuildOr(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildAnd(Expression lhs, Expression rhs, Analyzer& a);
        };
    }
}