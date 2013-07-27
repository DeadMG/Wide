#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class VoidType : public MetaType {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a);
        };
    }
}