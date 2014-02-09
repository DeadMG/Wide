#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class StringType : public PrimitiveType {
            llvm::LLVMContext* con;
        public:
            StringType() {}
        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;        
            clang::QualType GetClangType(Wide::ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        }; 
    }
}