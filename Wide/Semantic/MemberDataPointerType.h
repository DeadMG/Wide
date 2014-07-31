#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        // Itanium ABI 2.3
        // A pointer to data member is an offset from the base address of the class object containing it, represented as a ptrdiff_t. 
        // It has the size and alignment attributes of a ptrdiff_t. A NULL pointer is represented as - 1.
        class MemberDataPointer : public PrimitiveType {
            Type* source;
            Type* dest;
            std::unique_ptr<OverloadResolvable> booltest;
        public:
            MemberDataPointer(Analyzer& a, Type* source, Type* dest);
            llvm::Type* GetLLVMType(llvm::Module* mod) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access) override final;
        };
    }
}