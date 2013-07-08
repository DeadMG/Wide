#include <Codegen/LLVMOptions.h>
#include <llvm/Transforms/Scalar.h>

std::unique_ptr<llvm::Pass> Wide::Options::CreateDeadCodeElimination() {
    return std::unique_ptr<llvm::Pass>(llvm::createDeadCodeEliminationPass());
}