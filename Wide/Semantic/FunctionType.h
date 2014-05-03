#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>

namespace llvm {
    class PointerType;
}

namespace Wide {
    namespace Semantic {
        class FunctionType : public PrimitiveType {
            Type* ReturnType;
            std::vector<Type*> Args;
            Analyzer& analyzer;
        public:
            FunctionType(Type* ret, std::vector<Type*> args, Analyzer& a)
                : ReturnType(ret), Args(std::move(args)), analyzer(a) {}
        
            llvm::PointerType* GetLLVMType(Codegen::Generator& g) override final;

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& from, Analyzer& a) override final;
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetReturnType() { return ReturnType; }
            std::vector<Type*> GetArguments() { return Args; }
            std::string explain(Analyzer& a) override final;
        };
    }
}
