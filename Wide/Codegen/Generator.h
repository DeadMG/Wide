#pragma once

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        class Function;
        class Analyzer;
    }
    namespace Codegen {
        class Generator {
        public:
            std::unique_ptr<llvm::Module> module;
            llvm::IRBuilder<>& builder;
            Semantic::Analyzer* a;
        };
    }
}
