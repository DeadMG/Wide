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

Expression IntegralType::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (args.empty()) {
        Expression out;
        out.t = this;
        out.Expr = a.gen->CreateInt8Expression(0);
        return out;
    }

    // This function may be directly or indirectly invoked to construct from some other U. But currently we don't support that for primitives.
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an int8 from more than one argument.");

    // If args[0] is this, return it.
    if (args[0].t == this)
        return args[0];

    // If args[0] is T& or T&&, BuildValue().
    if (args[0].t->IsReference(this))
        return args[0].t->BuildValue(args[0], a);

    // No U conversions supported right now.
    throw std::runtime_error("Don't support constructing an int8 from another type right now.");
}

clang::QualType IntegralType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().CharTy;
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