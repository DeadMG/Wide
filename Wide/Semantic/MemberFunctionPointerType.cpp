#include <Wide/Semantic/MemberFunctionPointerType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Expression.h>

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
    return (analyzer.GetDataLayout().getPointerSizeInBits() / 8) * 2;
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
OverloadSet* MemberFunctionPointer::CreateOperatorOverloadSet(Lexer::TokenType what, Lexer::Access access) {
    if (what != &Lexer::TokenTypes::QuestionMark) return analyzer.GetOverloadSet();
    if (access != Lexer::Access::Public) return AccessMember(what, Lexer::Access::Public);
    if (!booltest)
        booltest = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* val, CodegenContext& con) {
                auto ptrbits = llvm::DataLayout(con.module->getDataLayout()).getPointerSizeInBits();
                auto ptr = con->CreateExtractValue(val, { 0 });
                auto constantzero = llvm::ConstantInt::get(llvm::IntegerType::get(con, ptrbits), uint64_t(0), true);
                return con->CreateZExt(con->CreateICmpNE(ptr, constantzero), llvm::IntegerType::getInt8Ty(con));
            });
        }, { this });
    return analyzer.GetOverloadSet(booltest.get());
}