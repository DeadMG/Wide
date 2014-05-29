#include <memory>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#pragma warning(pop)

namespace Wide {
    namespace Util {
        std::unique_ptr<llvm::Module> CreateModuleForTriple(std::string triple, llvm::LLVMContext& con);
    }
}