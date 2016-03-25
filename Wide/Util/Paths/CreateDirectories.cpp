#include <Wide/Util/Paths/CreateDirectories.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

void Wide::Paths::CreateDirectories(std::string path)
{
    llvm::sys::fs::create_directories(path.c_str());
}
