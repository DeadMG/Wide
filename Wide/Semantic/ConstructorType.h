#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            Type* emplace;
        public:
            ConstructorType(Type* con);
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression>, Context c) override final;

            Type* GetConstructedType() {
                return t;
            }
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}