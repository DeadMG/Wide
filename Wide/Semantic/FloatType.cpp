
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

clang::QualType FloatType::GetClangType(ClangUtil::ClangTU& from, Analyzer& a) {
    switch(bits) {
    case 16:
        return from.GetASTContext().HalfTy;
    case 32:
        return from.GetASTContext().FloatTy;
    case 64:
        return from.GetASTContext().DoubleTy;
    case 128:
        return from.GetASTContext().getTypeDeclType(from.GetASTContext().getFloat128StubType());
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

Codegen::Expression* FloatType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() > 1)
        throw std::runtime_error("Attempted to construct a floating-point type from more than one argument.");
    if (args.size() == 0)
        throw std::runtime_error("Attempted to default-construct a floating-point type.");
    if (args[0].t->Decay() == this)
        return a.gen->CreateStore(mem, args[0].BuildValue(a, where).Expr);
    auto fp = dynamic_cast<FloatType*>(args[0].BuildValue(a, where).Expr);
    if (!fp)
        throw std::runtime_error("Attempted to construct a floating-point type from another type that was not a floating-point type.");
    if (bits < fp->bits)
        return a.gen->CreateStore(mem, a.gen->CreateTruncate(args[0].BuildValue(a, where).Expr, GetLLVMType(a)));
    return a.gen->CreateStore(mem, a.gen->CreateFPExtension(args[0].BuildValue(a, where).Expr, GetLLVMType(a)));
}