#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Codegen/InitializeLLVM.h>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

using namespace Wide;
using namespace Driver;

NullGenerator::NullGenerator(std::string triple) {
    Codegen::InitializeLLVM();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
    layout = targetmachine->getDataLayout()->getStringRepresentation();
}
