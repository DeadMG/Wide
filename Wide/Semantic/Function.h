#pragma once

#include <Wide/Semantic/Type.h>
#include <boost/signals2.hpp>

namespace clang {
    class FunctionDecl;
}
namespace Wide {
    namespace Semantic {
        class FunctionSkeleton;
        class ClangFunctionType;
        class WideFunctionType;
        class Function : public Callable {
            FunctionSkeleton* skeleton;
            std::string llvmname;
            llvm::Function* llvmfunc = nullptr;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            std::vector<std::function<void(llvm::Module*)>> trampoline;
            Analyzer& analyzer;
            Wide::Util::optional<std::string> import_name;
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*>> clang_exports;
            std::unordered_set<Expression*> return_expressions;

            void ComputeReturnType();
        public:
            Function(Analyzer& a, FunctionSkeleton*, std::vector<Type*> args);

            llvm::Function* EmitCode(llvm::Module* module);
            WideFunctionType* GetSignature();

            std::string GetExportBody();
            void ComputeBody();
            void AddExportName(std::function<void(llvm::Module*)> mod);

            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            boost::signals2::signal<void(Type*)> ReturnTypeChanged;
            void AddReturnExpression(Expression*);
            std::vector<Type*> GetArguments() { return Args; }

            std::shared_ptr<Expression> GetThis();
            std::shared_ptr<Expression> GetStaticSelf();

            virtual ~Function() {}
        };
    }
}