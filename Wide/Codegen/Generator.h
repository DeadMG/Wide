#pragma once

#include <Wide/Codegen/LLVMOptions.h>

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
            llvm::LLVMContext context;
        public:
            Generator(std::string triple);
            std::unique_ptr<llvm::Module> module;
            void operator()(const Options::LLVM& opts);
        };
    }
}
