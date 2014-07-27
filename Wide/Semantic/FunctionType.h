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
            bool variadic;
            llvm::CallingConv::ID convention;
        public:
            FunctionType(Type* ret, std::vector<Type*> args, Analyzer& a, bool variadic, llvm::CallingConv::ID callingconvention)
                : ReturnType(ret), Args(std::move(args)), PrimitiveType(a), variadic(variadic), convention(callingconvention) {}
        
            llvm::PointerType* GetLLVMType(llvm::Module* module) override final;

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& from) override final;
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetReturnType() { return ReturnType; }
            std::vector<Type*> GetArguments() { return Args; }
            llvm::CallingConv::ID GetCallingConvention() { return convention; }
            std::string explain() override final;
        };
    }
}
