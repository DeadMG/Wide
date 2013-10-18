#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Parser/AST.h>

namespace Wide {
    namespace Semantic {
        struct SemanticExpression : public AST::Expression {
            Semantic::Expression e;
            SemanticExpression(Semantic::Expression expr, Lexer::Range where)
                : e(expr), AST::Expression(std::move(where)) {}
        };
    }
}