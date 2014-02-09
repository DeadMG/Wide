#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace AST {
        struct Lambda;
    }
    namespace Semantic {
        class LambdaType : public AggregateType {
            const AST::Lambda* lam;
        public:
            LambdaType(std::vector<Type*> capturetypes, const AST::Lambda* l, Analyzer& a);
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;
            ConcreteExpression BuildLambdaFromCaptures(std::vector<ConcreteExpression> exprs, Context c);
        };
    }
}