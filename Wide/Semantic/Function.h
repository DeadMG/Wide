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
        struct FunctionOverload;
        class Function : public Callable {
            std::vector<std::unique_ptr<Semantic::Error>> trampoline_errors;
            FunctionSkeleton* skeleton;
            std::string llvmname;
            llvm::Function* llvmfunc = nullptr;
            Type* ReturnType = nullptr;
            FunctionOverload* Overload;
            std::vector<std::function<void(llvm::Module*)>> trampoline;
            Analyzer& analyzer;
            Wide::Util::optional<std::string> import_name;
            std::unordered_set<Expression*> return_expressions;
            bool analyzed = false;

            void ComputeReturnType();
            
        public:
            Function(Analyzer& a, FunctionSkeleton*, FunctionOverload* overload);
            
            llvm::Function* EmitCode(llvm::Module* module);
            WideFunctionType* GetSignature();

            std::string GetExportBody();
            void ComputeBody();
            void AddExportName(std::function<void(llvm::Module*)> mod);

            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            boost::signals2::signal<void(Type*)> ReturnTypeChanged;
            void AddReturnExpression(Expression*);
            FunctionOverload* GetOverload() { return Overload; }

            std::shared_ptr<Expression> GetThis();
            std::shared_ptr<Expression> GetStaticSelf();

            virtual ~Function() {}
        };
    }
}