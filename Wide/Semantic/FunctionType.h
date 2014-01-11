#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>

namespace Wide {
    namespace Semantic {
        class FunctionType : public Type {
            Type* ReturnType;
            std::vector<Type*> Args;
        public:
            FunctionType(Type* ret, std::vector<Type*> a)
                : ReturnType(ret), Args(std::move(a)) {}
        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            clang::QualType GetClangType(ClangUtil::ClangTU& from, Analyzer& a) override;     
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override;

            Type* GetReturnType() { return ReturnType; }
            std::vector<Type*> GetArguments() { return Args; }
        };
    }
}
