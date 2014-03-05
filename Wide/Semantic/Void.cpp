#include <Wide/Semantic/Void.h>
#include <Wide/Semantic/ClangTU.h>

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

Wide::Util::optional<clang::QualType> VoidType::GetClangType(ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().VoidTy;
}
std::string VoidType::explain(Analyzer& a) {
    return "void";
}