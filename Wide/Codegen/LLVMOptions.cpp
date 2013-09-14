#include <Wide/Codegen/LLVMOptions.h>

#pragma warning(push, 0)
#include <llvm/Transforms/Scalar.h>
#pragma warning(pop)

std::unique_ptr<llvm::Pass> Wide::Options::CreateDeadCodeElimination() {
    return std::unique_ptr<llvm::Pass>(llvm::createDeadCodeEliminationPass());
}