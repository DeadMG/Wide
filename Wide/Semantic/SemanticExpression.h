#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Parser/AST.h>

namespace Wide {
    namespace Semantic {
        struct SemanticExpression : public AST::Expression {
            static const std::shared_ptr<std::string> semantic_expression_location;
            Semantic::Expression e;
            SemanticExpression(Semantic::Expression expr)
                : e(expr), AST::Expression(Lexer::Range(semantic_expression_location)) {}
        };
    }
}