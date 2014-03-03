#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class StringType : public PrimitiveType {
            std::string value;
        public:
            StringType(std::string val) : value(val) {}
        
            std::string GetValue() { return value; }
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            clang::QualType GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            std::string explain(Analyzer& a) override final;
        }; 
    }
}