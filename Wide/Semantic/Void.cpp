#include <Wide/Semantic/Void.h>
#include <Wide/Semantic/ClangTU.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::Type* VoidType::GetLLVMType(Codegen::Generator& g) {
    return llvm::Type::getVoidTy(g.module->getContext());
}

Wide::Util::optional<clang::QualType> VoidType::GetClangType(ClangTU& tu) {
    return tu.GetASTContext().VoidTy;
}
std::string VoidType::explain() {
    return "void";
}