#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Bool::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::Type::getInt8Ty(m->getContext());
    };
}

clang::QualType Bool::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    return where.GetASTContext().BoolTy;
}

Codegen::Expression* Bool::BuildBooleanConversion(Expression e, Analyzer& a) {
    return e.BuildValue(a).Expr;
}

Expression Bool::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {    
    if (args.empty()) {
        return Expression(this, a.gen->CreateIntegralExpression(0, false, GetLLVMType(a)));
    }

    // This function may be directly or indirectly invoked to construct from some other U. But currently we don't support that for primitives.
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct a bool from more than one argument.");

    // If args[0] is this, return it.
    if (args[0].t == this)
        return args[0];

    // If args[0] is T& or T&&, BuildValue().
    if (args[0].t->IsReference(this))
        return args[0].BuildValue(a);

    // No U conversions supported right now.
    throw std::runtime_error("Don't support constructing a bool from another type right now.");
}

Expression Bool::BuildOr(Expression lhs, Expression rhs, Analyzer& a) {
    return Expression(this, a.gen->CreateOrExpression(lhs.BuildBooleanConversion(a), rhs.BuildBooleanConversion(a)));
}

Expression Bool::BuildAnd(Expression lhs, Expression rhs, Analyzer& a) {
    return Expression(this, a.gen->CreateAndExpression(lhs.BuildBooleanConversion(a), rhs.BuildBooleanConversion(a)));
}

std::size_t Bool::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getTypeAllocSize(llvm::IntegerType::getInt8Ty(a.gen->context));
}
std::size_t Bool::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getABIIntegerTypeAlignment(8);
}