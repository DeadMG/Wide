#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class FunctionType;
        // Itanium ABI 2.3
        // A pointer to data member is an offset from the base address of the class object containing it, represented as a ptrdiff_t. 
        // It has the size and alignment attributes of a ptrdiff_t. A NULL pointer is represented as - 1.
        class MemberFunctionPointer : public PrimitiveType {
            Type* source;
            FunctionType* dest;
            std::unique_ptr<OverloadResolvable> booltest;
        public:
            MemberFunctionPointer(Analyzer& a, Type* source, FunctionType* dest);
            llvm::Type* GetLLVMType(llvm::Module* mod) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access) override final;
        };
    }
}