#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class StringType : public PrimitiveType {
            std::string value;
        public:
            StringType(std::string val, Analyzer& a) : value(val), PrimitiveType(a) {}
        
            std::string GetValue() { return value; }
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
        }; 
    }
}