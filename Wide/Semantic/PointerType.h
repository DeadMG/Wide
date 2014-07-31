#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> NullConstructor;
            std::unique_ptr<OverloadResolvable> DerivedConstructor;
            std::unique_ptr<OverloadResolvable> DereferenceOperator;
            std::unique_ptr<OverloadResolvable> BooleanOperator;
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point, Analyzer& a); 
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            bool IsSourceATarget(Type* first, Type* second, Type* context) override final;
            std::string explain() override final;
            Type* GetPointee() { return pointee; }
        };
    }
}
