#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Util/Codegen/InitializeLLVM.h>
#include <Wide/Util/Memory/MakeUnique.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#pragma warning(pop)

std::unique_ptr<llvm::Module> Wide::Util::CreateModuleForTriple(std::string triple, llvm::LLVMContext& context) {
    InitializeLLVM();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
	const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
    auto module = Wide::Memory::MakeUnique<llvm::Module>("Wide", context);
    module->setDataLayout(targetmachine->getSubtargetImpl()->getDataLayout()->getStringRepresentation());
    module->setTargetTriple(triple);
    return module;
}
