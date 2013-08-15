#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>

#pragma warning(push, 0)
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

Expression PointerType::BuildDereference(Expression val, Analyzer& a) {
	return Expression(a.AsLvalueType(pointee), val.BuildValue(a).Expr);
}

Codegen::Expression* PointerType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("Attempted to construct a pointer from more than one argument.");
    if (args.size() == 0)
        throw std::runtime_error("Attempted to default-construct a pointer.");
    args[0] = args[0].t->BuildValue(args[0], a);
    if (args[0].t->Decay() == this)
        return a.gen->CreateStore(mem, args[0].t->BuildValue(args[0], a).Expr);
    if (args[0].t->Decay() == a.GetNullType())
        return a.gen->CreateStore(mem, a.gen->CreateChainExpression(args[0].Expr, a.gen->CreateNull(GetLLVMType(a))));
    throw std::runtime_error("Attempted to construct a pointer from something that was not a pointer of the same type or null.");
}

Expression PointerType::BuildAssignment(Expression obj, Expression arg, Analyzer& a) {
    if (!obj.t->IsReference(this)) throw std::runtime_error("Attempted to assign to an rvalue pointer.");
    return Expression(this, a.gen->CreateChainExpression(BuildInplaceConstruction(obj.Expr, arg, a), obj.Expr));
}

Expression PointerType::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.t->BuildValue(lhs, a);
    rhs = rhs.t->BuildValue(rhs, a);
    if (lhs.t != this || rhs.t != this)
        throw std::runtime_error("Attempted to compare a pointer with something that was not a pointer of the same type.");
    return Expression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhs.Expr, rhs.Expr));
}

Codegen::Expression* PointerType::BuildBooleanConversion(Expression obj, Analyzer& a) {
    obj = obj.t->BuildValue(obj, a);
    return a.gen->CreateIsNullExpression(obj.Expr);
}

std::size_t PointerType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getPointerSize();
}
std::size_t PointerType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getPointerABIAlignment();
}