#include <Wide/Util/Codegen/GetMCJITProcessTriple.h>

#pragma warning(push, 0)
#include <llvm/Support/Host.h>
#include <llvm/ADT/Triple.h>
#pragma warning(pop)

std::string Wide::Util::GetMCJITProcessTriple() {
#ifdef _MSC_VER
    return "i686-w64-windows-gnu-elf";
#endif
    auto triple = llvm::sys::getProcessTriple();
    // Bug with x86-64 systems where getProcessTriple returns x86_64 and it's not recognized as the subtarget x86-64.
    if (triple[3] == '_')
        triple[3] = '-';
    return triple;
}
