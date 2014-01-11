#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Parser/AST.h>

namespace Wide {
    namespace Semantic {
        struct SemanticExpression : public AST::Expression {
            Semantic::ConcreteExpression e;
            SemanticExpression(Semantic::ConcreteExpression expr, Lexer::Range where)
                : e(expr), AST::Expression(std::move(where)) {}
        };
    }
}