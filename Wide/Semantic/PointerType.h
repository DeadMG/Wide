#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public PrimitiveType {
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point);
            ConcreteExpression BuildDereference(ConcreteExpression obj, Context c) override final;
            Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Context c) override final;
            clang::QualType GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&) override final;
            bool IsA(Type* self, Type* other, Analyzer& a) override final;
        };
    }
}
