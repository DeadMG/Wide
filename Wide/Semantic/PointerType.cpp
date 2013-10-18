#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

PointerType::PointerType(Type* point) {
    pointee = point;
}

clang::QualType PointerType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getPointerType(pointee->GetClangType(tu, a));
}

std::function<llvm::Type*(llvm::Module*)> PointerType::GetLLVMType(Analyzer& a) {
    return [=, &a](llvm::Module* mod) {
        auto ty = pointee->GetLLVMType(a)(mod);
        if (ty->isVoidTy())
            ty = llvm::IntegerType::getInt8Ty(mod->getContext());
        return llvm::PointerType::get(ty, 0);
    };
}

ConcreteExpression PointerType::BuildDereference(ConcreteExpression val, Analyzer& a, Lexer::Range where) {
    return ConcreteExpression(a.GetLvalueType(pointee), val.BuildValue(a, where).Expr);
}

Codegen::Expression* PointerType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() > 1)
        throw std::runtime_error("Attempted to construct a pointer from more than one argument.");
    if (args.size() == 0)
        throw std::runtime_error("Attempted to default-construct a pointer.");
    args[0] = args[0].BuildValue(a, where);
    if (args[0].t == this)
        return a.gen->CreateStore(mem, args[0].Expr);
    if (args[0].t->Decay() == a.GetNullType())
        return a.gen->CreateStore(mem, a.gen->CreateChainExpression(args[0].Expr, a.gen->CreateNull(GetLLVMType(a))));
    throw std::runtime_error("Attempted to construct a pointer from something that was not a pointer of the same type or null.");
}

ConcreteExpression PointerType::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where) {
    auto lhsval = lhs.BuildValue(a, where);
    auto rhsval = rhs.BuildValue(a, where);

    // If we're not a match, permit ADL to take over. Else, generate the primitive operator.
    if (lhs.t->Decay() != this || rhs.t->Decay() != this)
       return Type::BuildBinaryExpression(lhs, rhs, type, a, where);

    if (type == Lexer::TokenType::EqCmp) {
        return ConcreteExpression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhsval.Expr, rhsval.Expr));
    }
    
    return Type::BuildBinaryExpression(lhs, rhs, type, a, where);
}

Codegen::Expression* PointerType::BuildBooleanConversion(ConcreteExpression obj, Analyzer& a, Lexer::Range where) {
    return a.gen->CreateIsNullExpression(obj.BuildValue(a, where).Expr);
}

std::size_t PointerType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t PointerType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}