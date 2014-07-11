#include <Wide/Semantic/MemberFunctionPointerType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>

#pragma warning(push, 0)
#include <llvm/IR/DataLayout.h>
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

MemberFunctionPointer::MemberFunctionPointer(Analyzer& a, Type* source, FunctionType* dest)
: PrimitiveType(a), source(source), dest(dest) {}

llvm::Type* MemberFunctionPointer::GetLLVMType(llvm::Module* mod) {
    auto intty = llvm::IntegerType::get(mod->getContext(), analyzer.GetDataLayout().getPointerSizeInBits());
    return llvm::StructType::get(intty, intty, nullptr);
}
std::size_t MemberFunctionPointer::size() {
    return analyzer.GetDataLayout().getPointerSizeInBits() / 8;
}
std::size_t MemberFunctionPointer::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
std::string MemberFunctionPointer::explain() {
    std::string args;
    args += "{";
    for (auto type : dest->GetArguments())
        args += type->explain() + " ,";
    if (!dest->GetArguments().empty()) {
        args.pop_back();
        args.pop_back();
    }
    args += "}";
    return source->explain() + ".member_function_pointer(" + dest->GetReturnType()->explain() + ", " + args + ")";
}
Wide::Util::optional<clang::QualType> MemberFunctionPointer::GetClangType(ClangTU& TU) {
    auto srcty = source->GetClangType(TU);
    auto destty = dest->GetClangType(TU);
    if (!srcty || !destty) return Util::none;    
    return TU.GetASTContext().getMemberPointerType(*destty, srcty->getTypePtr());
}