#pragma once
#include <Wide/Semantic/Type.h>
namespace Wide {
    namespace Semantic {
        class FloatType : public PrimitiveType {
            unsigned bits;
        public:
            FloatType(unsigned bit) : bits(bit) {}

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;

            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}