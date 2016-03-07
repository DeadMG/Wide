#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class TupleType : public AggregateType {
            std::vector<Type*> contents;
        public:
            TupleType(std::vector<Type*> types, Analyzer& a);
            std::vector<Type*> GetMembers() override final { return contents; }

            std::shared_ptr<Expression> ConstructFromLiteral(std::vector<std::shared_ptr<Expression>> exprs, Context c);
            bool IsSourceATarget(Type* first, Type* second, Location context) override final;
            std::string explain() override final;
        };
    }
}