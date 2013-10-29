#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            Type* emplace;
        public:
            ConstructorType(Type* con);
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression>, Analyzer& a, Lexer::Range where) override;

            Type* GetConstructedType() {
                return t;
            }
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Analyzer& a, Lexer::Range where) override;
            Wide::Util::optional<Expression> PointerAccessMember(ConcreteExpression obj, std::string name, Analyzer& a, Lexer::Range where) override;
            using Type::BuildValueConstruction;
        };
    }
}