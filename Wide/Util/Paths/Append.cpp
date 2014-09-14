#include <Wide/Util/Paths/Append.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

std::string Wide::Paths::Append(std::string a, std::string b) {
    return Wide::Paths::Append(a, { b });
}

std::string Wide::Paths::Append(std::string a, std::initializer_list<std::string> others) {
    llvm::SmallVector<char, 200> Why(a.begin(), a.end());
    for (auto&& str : others) {
        llvm::sys::path::append(Why, str);
    }
    return std::string(Why.begin(), Why.end());
}