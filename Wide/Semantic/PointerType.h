#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> NullConstructor;
            std::unique_ptr<OverloadResolvable> DerivedConstructor;
            std::unique_ptr<OverloadResolvable> DereferenceOperator;
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point, Analyzer& a); 
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access) override final;
            std::unique_ptr<Expression> BuildBooleanConversion(std::unique_ptr<Expression>, Context) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            std::string explain() override final;
            Type* GetPointee() { return pointee; }
        };
    }
}
