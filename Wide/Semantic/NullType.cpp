#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<clang::QualType> NullType::GetClangType(ClangTU& TU) {
    return TU.GetASTContext().NullPtrTy;
}
// Odd choice but required for Clang interop.
llvm::Type* NullType::GetLLVMType(llvm::Module* module) {
    return llvm::IntegerType::getInt8PtrTy(module->getContext());
}
std::size_t NullType::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t NullType::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
std::string NullType::explain() {
    return "null";
}