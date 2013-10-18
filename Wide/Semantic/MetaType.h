#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class MetaType : public Type {
        public:
            using Type::BuildValueConstruction;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
        };
    }
}