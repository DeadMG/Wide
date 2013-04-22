#include "StringType.h"
#include "ClangTU.h"

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> StringType::GetLLVMType(Analyzer& a) { 
    return [](llvm::Module* m) -> llvm::Type* {
        return llvm::PointerType::getInt8PtrTy(m->getContext());
    };
}
clang::QualType StringType::GetClangType(Wide::ClangUtil::ClangTU& TU, Analyzer& a) {
    auto&& astcon = TU.GetDeclContext()->getParentASTContext();
    return astcon.getPointerType(astcon.CharTy);
}