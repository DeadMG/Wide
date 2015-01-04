#include <Wide/Semantic/Type.h>
#include <boost/signals2.hpp>

namespace Wide {
    namespace Semantic {
        class FunctionSkeleton;
        class WideFunctionType;
        class Function : public Callable {
            FunctionSkeleton* skeleton;
            std::string llvmname;
            llvm::Function* llvmfunc = nullptr;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            std::vector<std::function<void(llvm::Module*)>> trampoline;
            Analyzer& analyzer;
            boost::signals2::signal<void(Type*)> ReturnTypeChanged;
            Wide::Util::optional<std::string> import_name;

            void ComputeReturnType();
        public:
            Function(Analyzer& a, FunctionSkeleton*, std::vector<Type*> args);
            llvm::Function* EmitCode(llvm::Module* module);
            WideFunctionType* GetSignature();

            void ComputeBody();
            void AddExportName(std::function<void(llvm::Module*)> mod);

            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final;

            virtual ~Function() {}
        };
    }
}