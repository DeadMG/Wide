#pragma once

#include <Wide/Semantic/Type.h>
#include <boost/signals2.hpp>

namespace clang {
    class FunctionDecl;
}
namespace Wide {
    namespace Semantic {
        class ClangFunctionType;
        class WideFunctionType;
        namespace Functions {
            class FunctionSkeleton;
            class Function : public Callable {
                std::vector<std::unique_ptr<Semantic::Error>> trampoline_errors;
                FunctionSkeleton* skeleton;
                std::string llvmname;
                llvm::Function* llvmfunc = nullptr;
                Type* ReturnType = nullptr;
                std::vector<Type*> Args;
                std::vector<std::function<void(llvm::Module*)>> trampoline;
                Analyzer& analyzer;
                Wide::Util::optional<std::string> import_name;
                std::unordered_set<Expression*> return_expressions;
                bool analyzed = false;

                void ComputeReturnType();

            public:
                Function(Analyzer& a, FunctionSkeleton*, std::vector<Type*> args);

                llvm::Function* EmitCode(llvm::Module* module);
                WideFunctionType* GetSignature();

                void ComputeBody();
                void AddExportName(std::function<void(llvm::Module*)> mod);

                std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final;
                std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final;

                boost::signals2::signal<void(Type*)> ReturnTypeChanged;
                void AddReturnExpression(Expression*);
                std::vector<Type*> GetArguments() { return Args; }

                std::shared_ptr<Expression> GetStaticSelf();

                virtual ~Function() {}
            };
        }
    }
}