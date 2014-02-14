#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> StringType::GetLLVMType(Analyzer& a) { 
    return [](llvm::Module* m) -> llvm::Type* {
        return llvm::PointerType::getInt8PtrTy(m->getContext());
    };
}
clang::QualType StringType::GetClangType(ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().getPointerType(TU.GetASTContext().CharTy);
}

std::size_t StringType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t StringType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}