#include "ClangOptions.h"
#include "LLVMOptions.h"

namespace Wide {
    void Compile(
        const Options::Clang&,
        const Options::LLVM&,
        std::vector<std::string> files
    );
}