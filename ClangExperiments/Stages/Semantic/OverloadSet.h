#pragma once

#include "PrimitiveType.h"
#include <memory>

namespace Wide {
    namespace AST {
        struct FunctionOverloadSet;
    }
    namespace Semantic {
        class Function;
        class OverloadSet : public PrimitiveType {
            std::vector<Function*> funcs;
            std::shared_ptr<llvm::Type*> ty;
        public:
            OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
        };
    }
}