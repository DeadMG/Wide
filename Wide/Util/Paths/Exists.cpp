#include <Wide/Util/Paths/Exists.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

bool Wide::Paths::Exists(std::string path)
{
    return llvm::sys::fs::exists(path.c_str());
}
