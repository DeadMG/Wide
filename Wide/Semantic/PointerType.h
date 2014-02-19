#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public PrimitiveType {
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point); 
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access, Analyzer& a) override final;
            Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Context c) override final;
            clang::QualType GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&, Lexer::Access access) override final;
            bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) override final;
        };
    }
}
