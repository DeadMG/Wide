#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace AST {
        struct Lambda;
    }
    namespace Semantic {
        class LambdaType : public AggregateType, public MemberFunctionContext {
            const AST::Lambda* lam;
            std::unordered_map<std::string, std::size_t> names;
        public:
            LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const AST::Lambda* l, Analyzer& a);
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;
            ConcreteExpression BuildLambdaFromCaptures(std::vector<ConcreteExpression> exprs, Context c);

            Wide::Util::optional<ConcreteExpression> LookupCapture(ConcreteExpression self, std::string name, Context c);
            std::string explain(Analyzer& a) override final;
       };
    }
}