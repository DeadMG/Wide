#include <Wide/Util/Codegen/GetMCJITProcessTriple.h>

#pragma warning(push, 0)
#include <llvm/Support/Host.h>
#include <llvm/ADT/Triple.h>
#pragma warning(pop)

std::string Wide::Util::GetMCJITProcessTriple() {
#ifdef _MSC_VER
    return "i686-w64-windows-gnu-elf";
#endif
    return llvm::sys::getProcessTriple();
}