#include <Wide/Util/Codegen/InitializeLLVM.h>
#include <mutex>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#pragma warning(pop)

namespace {
    std::once_flag initialize_llvm;
}
void Wide::Util::InitializeLLVM() {
    std::call_once(initialize_llvm, [] {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeNativeTarget();
        llvm::InitializeAllAsmParsers();
    });
}