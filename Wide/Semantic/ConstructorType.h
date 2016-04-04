#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            std::unique_ptr<Type> array;
            std::unique_ptr<Type> members;
        public:
            ConstructorType(Type* con, Analyzer& a);
            std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            Type* GetConstructedType() {
                return t;
            }
            std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::string explain() override final;
        };
    }
}