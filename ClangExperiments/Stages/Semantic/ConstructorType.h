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
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression PointerAccessMember(Expression obj, std::string name, Analyzer& a);
        };
    }
}