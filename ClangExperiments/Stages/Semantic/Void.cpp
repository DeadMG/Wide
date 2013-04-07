#include "Void.h"
#include "ClangTU.h"

#pragma warning(push, 0)

#include <clang/AST/ASTContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> VoidType::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) -> llvm::Type* {
        return llvm::Type::getVoidTy(m->getContext());
    };
}

clang::QualType VoidType::GetClangType(ClangUtil::ClangTU& tu) {
    return tu.GetASTContext().VoidTy;
}