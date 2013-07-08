#pragma once

#include <Semantic/PrimitiveType.h>

namespace Wide {
    namespace Semantic {
        class StringType : public PrimitiveType {
            llvm::LLVMContext* con;
        public:
            StringType() {}
        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);        
            clang::QualType GetClangType(Wide::ClangUtil::ClangTU& TU, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        }; 
    }
}