
#include <Wide/Semantic/FloatType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Lexer/Token.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DataLayout.h>
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

#pragma warning(disable : 4715)
llvm::Type* GetLLVMTypeForBits(unsigned bits, llvm::LLVMContext& con) {
    switch(bits) {
    case 16:
        return llvm::Type::getHalfTy(con);
    case 32:
        return llvm::Type::getFloatTy(con);
    case 64:
        return llvm::Type::getDoubleTy(con);
    case 128:
        return llvm::Type::getFP128Ty(con);
    }
    assert(false && "Bad number of bits for floating-point type.");
}

clang::QualType FloatType::GetClangType(ClangTU& from, Analyzer& a) {
    switch(bits) {
    case 16:
        return from.GetASTContext().HalfTy;
    case 32:
        return from.GetASTContext().FloatTy;
    case 64:
        return from.GetASTContext().DoubleTy;
    }
    assert(false && "Bad number of bits for floating-point type.");
}
#pragma warning(disable : 4715)
std::function<llvm::Type*(llvm::Module*)> FloatType::GetLLVMType(Analyzer& a) {
    return [=](llvm::Module* m) -> llvm::Type* {
        return GetLLVMTypeForBits(bits, m->getContext());
    };
}

#pragma warning(disable : 4244)
std::size_t FloatType::size(Analyzer& a) {
    return a.gen->GetDataLayout().getTypeAllocSize(GetLLVMTypeForBits(bits, a.gen->GetContext()));
}
std::size_t FloatType::alignment(Analyzer& a) {
    return a.gen->GetDataLayout().getABITypeAlignment(GetLLVMTypeForBits(bits, a.gen->GetContext()));
}
#pragma warning(default : 4244)

std::string FloatType::explain(Analyzer& a) {
    return "float" + std::to_string(bits);
}