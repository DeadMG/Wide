#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        class FunctionType : public PrimitiveType {
            Type* ReturnType;
            std::vector<Type*> Args;
        public:
            FunctionType(Type* ret, std::vector<Type*> args, Analyzer& a)
                : ReturnType(ret), Args(std::move(args)), PrimitiveType(a) {}
        
            llvm::PointerType* GetLLVMType(Codegen::Generator& g) override final;

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& from) override final;
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetReturnType() { return ReturnType; }
            std::vector<Type*> GetArguments() { return Args; }
            std::string explain() override final;
        };
    }
}
