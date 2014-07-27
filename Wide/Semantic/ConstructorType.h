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
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            Type* GetConstructedType() {
                return t;
            }
            std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::string explain() override final;
        };
        struct ExplicitConstruction : Expression {
            ExplicitConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c, Type* t)
            : c(c), self(std::move(self)) {
                result = t->BuildValueConstruction(std::move(args), c);
            }
            Context c;
            std::shared_ptr<Expression> self;
            std::shared_ptr<Expression> result;
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                self->GetValue(con);
                return result->GetValue(con);
            }
            Type* GetType() override final {
                return result->GetType();
            }
        };
    }
}