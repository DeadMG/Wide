#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class VoidType : public MetaType {
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            clang::QualType GetClangType(ClangTU& tu, Analyzer& a) override;
        };
    }
}