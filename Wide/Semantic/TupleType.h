#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class TupleType : public AggregateType {
            std::vector<Type*> contents;
        public:
            TupleType(std::vector<Type*> types, Analyzer& a);
            const std::vector<Type*>& GetContents() { return contents; }

            std::unique_ptr<Expression> ConstructFromLiteral(std::vector<std::unique_ptr<Expression>> exprs);
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            std::string explain() override final;
        };
    }
}