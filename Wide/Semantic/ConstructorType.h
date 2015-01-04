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
            std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            Type* GetConstructedType() {
                return t;
            }
            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::string explain() override final;
        };
        //struct ExplicitConstruction : Expression {
        //    ExplicitConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c, Type* t)
        //    : c(c), self(std::move(self)), ty(t) {
        //        result = t->BuildValueConstruction(std::move(args), c);
        //    }
        //    Context c;
        //    Type* ty;
        //    std::shared_ptr<Expression> self;
        //    std::shared_ptr<Expression> result;
        //    llvm::Value* ComputeValue(CodegenContext& con) override final {
        //        self->GetValue(con);
        //        return result->GetValue(con);
        //    }
        //    Type* GetType(Function* f) override final {
        //        return ty;
        //    }
        //};
    }
}