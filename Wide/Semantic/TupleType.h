#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class TupleType : public AggregateType {
            std::vector<Type*> contents;
        public:
            TupleType(std::vector<Type*> types, Analyzer& a);
            std::vector<Type*> GetMembers() override final { return contents; }

            std::unique_ptr<Expression> ConstructFromLiteral(std::vector<std::unique_ptr<Expression>> exprs, Context c);
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            std::string explain() override final;
        };
    }
}