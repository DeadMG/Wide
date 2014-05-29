#pragma once
#include <Wide/Semantic/Type.h>
namespace Wide {
    namespace Semantic {
        class FloatType : public PrimitiveType {
            unsigned bits;
        public:
            FloatType(unsigned bit, Analyzer& a) : bits(bit), PrimitiveType(a) {}

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;

            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
        };
    }
}