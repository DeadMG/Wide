#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Expression.h>
#include <vector>
#include <unordered_map>
#include <memory>

namespace llvm {
    class Function;
}
namespace clang {
    class FunctionDecl;
}
namespace Wide {
    namespace Parse {
        struct FunctionBase;
    }
    namespace Semantic {
        class WideFunctionType;
        class UserDefinedType;
        class ClangFunctionType;
        class TupleType;
        struct ControlFlowStatement {
            virtual void JumpForContinue(CodegenContext& con) = 0;
            virtual void JumpForBreak(CodegenContext& con) = 0;
        };
        struct Scope {
            // Automatically registers Scope to be owned by the parent.
            Scope(Scope* s);
            Scope* parent;
            std::vector<std::unique_ptr<Scope>> children;
            std::unordered_map<std::string, std::pair<std::shared_ptr<Expression>, Lexer::Range>> named_variables;
            std::vector<std::shared_ptr<Statement>> active;
            ControlFlowStatement* control_flow;
            std::shared_ptr<Expression> LookupLocal(std::string name);
            ControlFlowStatement* GetCurrentControlFlow();
        };
        struct Return {
            virtual Type* GetReturnType() = 0;
        };
        class FunctionSkeleton : public MetaType, public Callable {
            std::string llvmname;
            llvm::Function* llvmfunc = nullptr;
            Wide::Util::optional<Type*> ExplicitReturnType;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            const Parse::FunctionBase* fun;
            Type* context;
            std::string source_name;
            std::vector<std::function<void(llvm::Module*)>> trampoline;
            std::vector<std::shared_ptr<Expression>> parameters;
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*>> clang_exports;
            Wide::Util::optional<std::string> import_name;

            // You can only be exported as constructors of one, or nonstatic member of one, class.
            Wide::Util::optional<Semantic::ConstructorContext*> ConstructorContext;
            Wide::Util::optional<Type*> NonstaticMemberContext;
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            State s;
            void ComputeReturnType();
            std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
                return Type::BuildCall(BuildValueConstruction({}, c), std::move(args), c);
            }
            std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::unordered_set<Return*> returns;

            std::unique_ptr<Scope> root_scope;
        public:
            static void AddDefaultHandlers(Analyzer& a);
            void ComputeBody();
            llvm::Function* EmitCode(llvm::Module* module);
            FunctionSkeleton(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::string name, Type* nonstatic_context);

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& where) override final;

            std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            Type* GetContext() override final { return context; }
            Type* GetNonstaticMemberContext() { if (NonstaticMemberContext) return *NonstaticMemberContext; return nullptr; }

            WideFunctionType* GetSignature();
            std::shared_ptr<Expression> LookupLocal(Parse::Name name);
            Type* GetConstantContext() override final;
            std::string explain() override final;
            std::string GetSourceName() { return source_name; }
            std::shared_ptr<Expression> GetStaticSelf();
            void AddExportName(std::function<void(llvm::Module*)> mod);
            std::string GetExportBody();
            ~FunctionSkeleton();
        };
    }
}