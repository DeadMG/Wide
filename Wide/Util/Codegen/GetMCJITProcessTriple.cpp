#include <Wide/Util/Codegen/GetMCJITProcessTriple.h>

#pragma warning(push, 0)
#include <llvm/Support/Host.h>
#include <llvm/ADT/Triple.h>
#pragma warning(pop)

std::string Wide::Util::GetMCJITProcessTriple() {
    auto triple = llvm::Triple(llvm::sys::getProcessTriple());
#ifdef _MSC_VER
    //triple.setOS(llvm::Triple::OSType::MinGW32);
    triple.setEnvironment(llvm::Triple::EnvironmentType::Itanium);
#endif
    auto stringtrip = triple.getTriple();
#ifdef _MSC_VER
    // MCJIT can't handle non-ELF on Windows for some reason.
    stringtrip += "-elf";
#endif
    return stringtrip;
}