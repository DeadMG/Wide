
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

using namespace Wide;
using namespace Semantic;

#pragma warning(disable : 4715)

Wide::Util::optional<clang::QualType> FloatType::GetClangType(ClangTU& from) {
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
llvm::Type* FloatType::GetLLVMType(Codegen::Generator& g) {
    switch (bits) {
    case 16:
        return llvm::Type::getHalfTy(g.module->getContext());
    case 32:
        return llvm::Type::getFloatTy(g.module->getContext());
    case 64:
        return llvm::Type::getDoubleTy(g.module->getContext());
    case 128:
        return llvm::Type::getFP128Ty(g.module->getContext());
    }
    assert(false && "Bad number of bits for floating-point type.");
}

#pragma warning(disable : 4244)
std::size_t FloatType::size() {
    return bits / 8;
}
std::size_t FloatType::alignment() {
    return bits / 8;
}
#pragma warning(default : 4244)

std::string FloatType::explain() {
    return "float" + std::to_string(bits);
}