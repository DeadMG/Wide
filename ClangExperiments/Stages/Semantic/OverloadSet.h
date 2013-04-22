#pragma once

#include "PrimitiveType.h"
#include <memory>
#include <unordered_map>

namespace Wide {
    namespace AST {
        struct FunctionOverloadSet;
    }
    namespace Semantic {
        class Function;
        class OverloadSet : public PrimitiveType {
            std::vector<Function*> funcs;
            std::shared_ptr<llvm::Type*> ty;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
        public:
            OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
        };
    }
}