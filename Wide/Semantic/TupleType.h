#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class TupleType : public AggregateType {
            std::vector<Type*> contents;
        public:
            TupleType(std::vector<Type*> types, Analyzer& a);
            const std::vector<Type*>& GetContents() { return contents; }

            ConcreteExpression ConstructFromLiteral(std::vector<ConcreteExpression> exprs, Context c);
            bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}