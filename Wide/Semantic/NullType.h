#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        struct NullType : public MetaType {    
            NullType() {}
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;   
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}