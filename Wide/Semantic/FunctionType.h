#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#pragma warning(pop)

namespace clang {
    class FunctionProtoType;
    class CXXRecordDecl;
    namespace CodeGen {
        class CGFunctionInfo;
    }
}
namespace Wide {
    namespace Semantic {
        class WideFunctionType;
        class FunctionType : public PrimitiveType {
        public:
            FunctionType(Analyzer& a) : PrimitiveType(a) {}
        
            virtual llvm::PointerType* GetLLVMType(llvm::Module* module) override = 0;
            virtual std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override = 0;
            virtual Type* GetReturnType() = 0;
            virtual std::vector<Type*> GetArguments() = 0;           

            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
            // RHS is most derived type.
            static bool CanThunkFromFirstToSecond(FunctionType* lhs, FunctionType* rhs, Type* context, bool adjust);
            static std::shared_ptr<Expression> CreateThunk(std::shared_ptr<Expression> self, WideFunctionType* dest, Type* context);
            virtual std::function<void(llvm::Module*)> CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, std::string name, Type* context) = 0;
        };
        class WideFunctionType : public FunctionType {
            Type* ReturnType;
            std::vector<Type*> Args;
            llvm::CallingConv::ID convention;
            bool variadic;
        public:
            WideFunctionType(Type* ret, std::vector<Type*> args, Analyzer& a, llvm::CallingConv::ID callingconvention, bool variadic)
                : ReturnType(ret), Args(std::move(args)), FunctionType(a), convention(callingconvention), variadic(variadic) {}
            llvm::PointerType* GetLLVMType(llvm::Module* module) override final;
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            Type* GetReturnType() override final;
            std::vector<Type*> GetArguments() override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& from) override final;
            std::function<void(llvm::Module*)> CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, std::string name, Type* context) override final;
        };
        class ClangFunctionType : public FunctionType {
            const clang::FunctionProtoType* type;
            Wide::Util::optional<clang::QualType> self;
            ClangTU* from;
            const clang::CodeGen::CGFunctionInfo& GetCGFunctionInfo(llvm::Module* module);
        public:
            ClangFunctionType(Analyzer& a, const clang::FunctionProtoType* type, ClangTU* from, Wide::Util::optional<clang::QualType> self) : type(type), FunctionType(a), self(self), from(from) {}

            llvm::PointerType* GetLLVMType(llvm::Module* module) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& from) override final;
            Type* GetReturnType() override final;
            std::vector<Type*> GetArguments() override final;
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::function<void(llvm::Module*)> CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, std::string name, Type* context) override final;
            std::function<void(llvm::Module*)> CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, clang::FunctionDecl* decl, Type* context);
       };
    }
}
