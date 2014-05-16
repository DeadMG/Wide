#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        struct NullType : public MetaType {    
            NullType(Analyzer& a) : MetaType(a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TUa) override final;
            llvm::Type* GetLLVMType(Codegen::Generator& g) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            bool IsA(Type* self, Type* other, Lexer::Access) override final;
            std::string explain() override final;
        };
    }
}