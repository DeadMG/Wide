#include <Wide/Semantic/MemberDataPointerType.h>
#include <Wide/Semantic/Analyzer.h>

#pragma warning(push, 0)
#include <llvm/IR/DataLayout.h>
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

MemberDataPointer::MemberDataPointer(Analyzer& a, Type* source, Type* dest)
: PrimitiveType(a), source(source), dest(dest) {}

llvm::Type* MemberDataPointer::GetLLVMType(llvm::Module* mod) {
    return llvm::IntegerType::get(mod->getContext(), analyzer.GetDataLayout().getPointerSizeInBits());
}
std::size_t MemberDataPointer::size() {
    return analyzer.GetDataLayout().getPointerSizeInBits() / 8;
}
std::size_t MemberDataPointer::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
std::string MemberDataPointer::explain() {
    return source->explain() + ".member_pointer(" + dest->explain() + ")";
}
Wide::Util::optional<clang::QualType> MemberDataPointer::GetClangType(ClangTU& TU) {
    auto srcty = source->GetClangType(TU);
    auto destty = dest->GetClangType(TU);
    if (!srcty || !destty) return Util::none;
    return TU.GetASTContext().getMemberPointerType(*destty, srcty->getTypePtr());
}