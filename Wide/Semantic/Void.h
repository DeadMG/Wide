#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class VoidType : public MetaType {
        public:
            VoidType(Analyzer& a) : MetaType(a) {}
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            std::string explain() override final;
        };
    }
}