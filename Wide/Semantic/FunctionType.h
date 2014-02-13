#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>

namespace Wide {
    namespace Semantic {
        class FunctionType : public PrimitiveType {
            Type* ReturnType;
            std::vector<Type*> Args;
        public:
            FunctionType(Type* ret, std::vector<Type*> a)
                : ReturnType(ret), Args(std::move(a)) {}
        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            clang::QualType GetClangType(ClangTU& from, Analyzer& a) override final;
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;

            Type* GetReturnType() { return ReturnType; }
            std::vector<Type*> GetArguments() { return Args; }
        };
    }
}
