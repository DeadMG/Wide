#pragma once

#include "PrimitiveType.h"
#include <memory>
#include <unordered_map>

#ifdef _MSC_VER
#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)
#endif

namespace Wide {
    namespace AST {
        struct FunctionOverloadSet;
    }
    namespace Semantic {
        class Function;
        class UserDefinedType;
        class OverloadSet : public PrimitiveType {
            AST::FunctionOverloadSet* overset;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            UserDefinedType* nonstatic;
        public:
            OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a, UserDefinedType* nonstatic = nullptr);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
        };
    }
}