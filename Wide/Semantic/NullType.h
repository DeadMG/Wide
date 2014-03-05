#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        struct NullType : public MetaType {    
            NullType() {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}