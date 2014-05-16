#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class VoidType : public MetaType {
        public:
            VoidType(Analyzer& a) : MetaType(a) {}
            llvm::Type* GetLLVMType(Codegen::Generator& g) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            std::string explain() override final;
        };
    }
}