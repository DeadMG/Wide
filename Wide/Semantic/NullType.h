#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        struct NullType : public MetaType {    
            NullType(Analyzer& a) : MetaType(a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TUa) override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
        };
    }
}