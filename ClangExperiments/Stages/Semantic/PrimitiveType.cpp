#include "PrimitiveType.h"
#include "../Codegen/Expression.h"
#include "Analyzer.h"
#include "LvalueType.h"
#include "../Codegen/Generator.h"

using namespace Wide;
using namespace Semantic;

Codegen::Expression* PrimitiveType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 1) {
        if (args[0].t == this)
            return a.gen->CreateStore(mem, BuildValueConstruction(args, a).Expr);
        if (args[0].t->IsReference(this))
            return a.gen->CreateStore(mem, args[0].t->BuildValue(args[0], a).Expr);
        throw std::runtime_error("Attempt to in-place-construct a PrimitiveType with an argument of some type that was not T, T&, or T&&.");
    }
    throw std::runtime_error("Attempt to in-place construct a PrimitiveType with zero arguments.");
}

Expression PrimitiveType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    // x = y- x needs to be an lvalue.
    // Commented out because lambda capture-by-ref currently assigns to an rvalue reference.
    /*if (!dynamic_cast<LvalueType*>(lhs.t))
        throw std::runtime_error("Attempted to assign to a value or rvalue.");*/

    // Primitives need assignment by value, so convert RHS to a value.
    rhs = rhs.t->BuildValue(rhs, a);
    if (rhs.t != this)
        throw std::runtime_error("Cannot assign from one primitive type to another.");
    return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, rhs.Expr));
}