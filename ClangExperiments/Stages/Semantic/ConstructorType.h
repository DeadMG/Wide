#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {
        class ConstructorType : public Type {
            Type* t;
        public:
            ConstructorType(Type* con)
                : t(con) {}
            Expression BuildCall(Expression, std::vector<Expression>, Analyzer& a);

            Type* GetConstructedType() {
                return t;
            }
            Expression AccessMember(Expression, std::string name, Analyzer& a);
        };
    }
}