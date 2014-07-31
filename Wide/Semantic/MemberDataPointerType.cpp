#include <Wide/Semantic/MemberDataPointerType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Expression.h>

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
OverloadSet* MemberDataPointer::CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access) {
    if (what.size() != 1 || what.front() != &Lexer::TokenTypes::QuestionMark) return analyzer.GetOverloadSet();
    if (access != Parse::Access::Public) return AccessMember(what, Parse::Access::Public);
    if (!booltest) 
        booltest = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* val, CodegenContext& con) {
                auto ptrbits = llvm::DataLayout(con.module->getDataLayout()).getPointerSizeInBits();
                return con->CreateZExt(con->CreateICmpNE(val, llvm::ConstantInt::get(llvm::IntegerType::get(con, ptrbits), uint64_t(-1), true)), llvm::IntegerType::getInt8Ty(con));
            });
        }, { this });
    return analyzer.GetOverloadSet(booltest.get());
}