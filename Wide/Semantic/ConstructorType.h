#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            Type* emplace;
        public:
            ConstructorType(Type* con);
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression>, Context c) override;

            Type* GetConstructedType() {
                return t;
            }
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Context c) override;
            Wide::Util::optional<Expression> PointerAccessMember(ConcreteExpression obj, std::string name, Context c) override;
            using Type::BuildValueConstruction;
        };
    }
}