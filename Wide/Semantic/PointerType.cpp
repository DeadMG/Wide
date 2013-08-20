#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Lexer/Token.h>

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

Expression PointerType::BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType type, Analyzer& a) {
	auto lhsval = lhs.BuildValue(a);
	auto rhsval = rhs.BuildValue(a);

	// If we're not a match, permit ADL to take over. Else, generate the primitive operator.
	if (lhs.t->Decay() != this || rhs.t->Decay() != this)
	   return Type::BuildBinaryExpression(lhs, rhs, type, a);

	if (type == Lexer::TokenType::EqCmp) {
        return Expression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhsval.Expr, rhsval.Expr));
	}

	// Let ADL take over if we are not an lvalue.
	if (!a.IsLvalueType(lhs.t))
		return Type::BuildBinaryExpression(lhs, rhs, type, a);

	/*switch(type) {
	case Lexer::TokenType::Assignment:
		return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, rhsval.Expr));
	}*/
	// Permit ADL to find any funky operators the user may have defined or defaults.
	return Type::BuildBinaryExpression(lhs, rhs, type, a);
}

Codegen::Expression* PointerType::BuildBooleanConversion(Expression obj, Analyzer& a) {
    obj = obj.t->BuildValue(obj, a);
    return a.gen->CreateIsNullExpression(obj.Expr);
}

std::size_t PointerType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t PointerType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}