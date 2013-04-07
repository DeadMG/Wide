#pragma once

#include "PrimitiveType.h"

namespace Wide {
    namespace Semantic {
        class Bool : public PrimitiveType {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU&);

            Codegen::Expression* BuildBooleanConversion(Expression, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
        };
    }
}