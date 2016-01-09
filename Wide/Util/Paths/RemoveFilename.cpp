#include <Wide/Util/Paths/RemoveFilename.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop, 0)

std::string Wide::Paths::RemoveFilename(std::string input)
{
    llvm::SmallString<200> fuck;
    for (auto& character : input)
        fuck.push_back(character);
    llvm::sys::path::remove_filename(fuck);
    return std::string(fuck.begin(), fuck.end());
}
