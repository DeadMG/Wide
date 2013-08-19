#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            Type* emplace;
        public:
            ConstructorType(Type* con);
            Expression BuildCall(Expression, std::vector<Expression>, Analyzer& a) override;

            Type* GetConstructedType() {
                return t;
            }
            Wide::Util::optional<Expression> AccessMember(Expression, std::string name, Analyzer& a) override;
            Wide::Util::optional<Expression> PointerAccessMember(Expression obj, std::string name, Analyzer& a) override;
            using Type::BuildValueConstruction;
        };
    }
}