#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ConstructorType : public MetaType {
            Type* t;
            std::unique_ptr<Type> emplace;
        public:
            ConstructorType(Type* con, Analyzer& a);
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;

            Type* GetConstructedType() {
                return t;
            }
            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) override final;
            std::string explain() override final;
        };
        struct ExplicitConstruction : Expression {
            ExplicitConstruction(std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> args, Context c, Type* t)
            : c(c), self(std::move(self)) {
                result = t->BuildValueConstruction(std::move(args), c);
            }
            Context c;
            std::unique_ptr<Expression> self;
            std::unique_ptr<Expression> result;
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                result->DestroyLocals(g, bb);
                self->DestroyLocals(g, bb);
            }
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                self->GetValue(g, bb);
                return result->GetValue(g, bb);
            }
            Type* GetType() override final {
                return result->GetType();
            }
        };
    }
}