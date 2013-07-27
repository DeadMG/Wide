#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            Type* emplace;
        public:
            ConstructorType(Type* con);
            Expression BuildCall(Expression, std::vector<Expression>, Analyzer& a);

            Type* GetConstructedType() {
                return t;
            }
            Expression AccessMember(Expression, std::string name, Analyzer& a);
            Expression PointerAccessMember(Expression obj, std::string name, Analyzer& a);
            using Type::BuildValueConstruction;
        };
    }
}