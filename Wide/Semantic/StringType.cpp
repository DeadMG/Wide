#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::Type* StringType::GetLLVMType(llvm::Module* module) { 
    return llvm::PointerType::getInt8PtrTy(module->getContext());
}
Wide::Util::optional<clang::QualType> StringType::GetClangType(ClangTU& TU) {
    return TU.GetASTContext().getPointerType(TU.GetASTContext().getConstType(TU.GetASTContext().CharTy));
}

std::size_t StringType::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t StringType::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
std::string StringType::explain() {
    return "string";
}
bool StringType::IsConstant() {
    return true;
}