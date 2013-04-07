#include "PrimitiveType.h"
#include "../Codegen/Expression.h"
#include "Analyzer.h"
#include "LvalueType.h"
#include "../Codegen/Generator.h"

using namespace Wide;
using namespace Semantic;

Codegen::Expression* PrimitiveType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    // The category was already determined by the caller. We only need to construct in.
     return a.gen->CreateStore(mem, BuildValueConstruction(args, a).Expr);
}

Expression PrimitiveType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    // x = y- x needs to be an lvalue.
    if (!dynamic_cast<LvalueType*>(lhs.t))
        throw std::runtime_error("Attempted to assign to a value or rvalue.");

    // Primitives need assignment by value, so convert RHS to a value.
    rhs = rhs.t->BuildValue(rhs, a);
    if (rhs.t != this)
        throw std::runtime_error("Cannot assign from one primitive type to another.");
    Expression out;
    out.t = lhs.t;
    out.Expr = a.gen->CreateStore(lhs.Expr, rhs.Expr);
    return out;
}