#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class TupleType : public AggregateType {
        public:
            TupleType(std::vector<Type*> types, Analyzer& a);

            ConcreteExpression ConstructFromLiteral(std::vector<ConcreteExpression> exprs, Context c);
            bool IsA(Type* self, Type* other, Analyzer& a) override final;
        };
    }
}