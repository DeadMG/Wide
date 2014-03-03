#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/PointerType.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

clang::QualType NullType::GetClangType(ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().NullPtrTy;
}
std::function<llvm::Type*(llvm::Module*)> NullType::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::IntegerType::getInt8PtrTy(m->getContext());
    };
}
std::size_t NullType::size(Analyzer& a) {
    return a.gen->GetDataLayout().getPointerSize();
}
std::size_t NullType::alignment(Analyzer& a) {
    return a.gen->GetDataLayout().getPointerABIAlignment();
}
bool NullType::IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) {
    if (dynamic_cast<PointerType*>(other))
        return true;
    if (dynamic_cast<PointerType*>(other->Decay()) && IsRvalueType(other))
        return true;
    return Type::IsA(self, other, a, access);
}
std::string NullType::explain(Analyzer& a) {
    return "null";
}