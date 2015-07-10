#pragma once
#include <memory>
namespace llvm {
    class Module;
}
namespace Wide {
    namespace Util {
        std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& mod);
    }
}