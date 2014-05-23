#include <Wide/Codegen/Generator.h>
#include <Wide/Util/Memory/MakeUnique.h>

#pragma warning(push, 0)
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/PassManager.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Transforms/IPO.h>
#pragma warning(pop)

using namespace Wide;
using namespace Codegen;

static bool fuck_llvm = false;
void Codegen::InitializeLLVM() {
    if (!fuck_llvm) {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeNativeTarget();
        llvm::InitializeAllAsmParsers();
        fuck_llvm = true;
    }
}

Generator::Generator(std::string triple)
: context() 
{
    InitializeLLVM();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
    module = Wide::Memory::MakeUnique<llvm::Module>("Wide", context);
    module->setDataLayout(targetmachine->getDataLayout()->getStringRepresentation());
    module->setTargetTriple(triple);
}

void Generator::operator()(const Options::LLVM& opts) {
    llvm::PassManager pm;

    pm.add(new llvm::DataLayout(module->getDataLayout()));
    for (auto&& pass : opts.Passes)
        pm.add(pass->createPass(pass->getPassID()));

    pm.run(*module);
}