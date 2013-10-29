#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class PointerType : public Type {
            Type* pointee;
        public:
            using Type::BuildInplaceConstruction;

            PointerType(Type* point);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            ConcreteExpression BuildDereference(ConcreteExpression obj, Analyzer& a, Lexer::Range where) override;
            Codegen::Expression* BuildBooleanConversion(ConcreteExpression val, Analyzer& a, Lexer::Range where) override;
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}
