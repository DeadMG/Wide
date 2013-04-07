#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {
        class StringType : public Type {
            llvm::LLVMContext* con;
        public:
            StringType() {}
        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);        
            clang::QualType GetClangType(Wide::ClangUtil::ClangTU& TU);     
        }; 
    }
}