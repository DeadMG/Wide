#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {
        class PointerType : public Type {
            Type* pointee;
        public:
            PointerType(Type* point);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression BuildDereference(Expression obj, Analyzer& a);
            Codegen::Expression* BuildBooleanConversion(Expression val, Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        };
    }
}
