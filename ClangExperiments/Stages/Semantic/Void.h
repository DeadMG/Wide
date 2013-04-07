#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {
        class VoidType : public Type {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& tu);
        };
    }
}