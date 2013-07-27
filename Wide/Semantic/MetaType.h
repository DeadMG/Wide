#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class MetaType : public Type {
        public:
            using Type::BuildValueConstruction;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
        };
    }
}