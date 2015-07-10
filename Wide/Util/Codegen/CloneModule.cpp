#include "CloneModule.h"

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Cloning.h>
#pragma warning(pop)

std::unique_ptr<llvm::Module> Wide::Util::CloneModule(const llvm::Module & mod)
{
    return std::unique_ptr<llvm::Module>(llvm::CloneModule(&mod));
}
