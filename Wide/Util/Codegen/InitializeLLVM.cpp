#include <Wide/Util/Codegen/InitializeLLVM.h>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)


static bool fuck_llvm = false;
void Wide::Codegen::InitializeLLVM() {
    if (!fuck_llvm) {        
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeNativeTarget();
        llvm::InitializeAllAsmParsers();
        fuck_llvm = true;
    }
}