#include "IntegralType.h"
#include "Analyzer.h"
#include "ClangTU.h"
#include "../Codegen/Generator.h"
#include "../Codegen/Expression.h"
#include "RvalueType.h"

#pragma warning(push, 0)

#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

clang::QualType IntegralType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().UnsignedCharTy;
}
std::function<llvm::Type*(llvm::Module*)> IntegralType::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::IntegerType::get(m->getContext(), 8);
    };
}
Expression IntegralType::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    if (rhs.t != this) {
        std::vector<Expression> args;
        args.push_back(rhs);
        rhs = BuildValueConstruction(args, a);
    }
    Expression out;
    out.t = this;
    out.Expr = a.gen->CreateRightShift(lhs.Expr, rhs.Expr);
    return out;
}
Expression IntegralType::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    if (rhs.t != this) {
        std::vector<Expression> args;
        args.push_back(rhs);
        rhs = BuildValueConstruction(args, a);
    }
    Expression out;
    out.t = this;
    out.Expr = a.gen->CreateLeftShift(lhs.Expr, rhs.Expr);
    return out;
}
Expression IntegralType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.t->BuildValue(lhs, a);
    rhs = rhs.t->BuildValue(rhs, a);
    return Expression(a.Boolean, a.gen->CreateSLT(lhs.Expr, rhs.Expr));
}