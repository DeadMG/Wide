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
        namespace Functions {
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
                boost::signals2::signal<void(Type*)> OnReturnType;
                virtual Type* GetReturnType() = 0;
            };
            class FunctionSkeleton {
                bool analyzed = false;
            protected:
                Location GetLocalContext();

                std::unordered_map<const Parse::Expression*, std::unique_ptr<Semantic::Error>> ExportErrors;
                std::unique_ptr<Semantic::Error> ExplicitReturnError;
                std::unordered_map<const Parse::VariableInitializer*, std::unique_ptr<Semantic::Error>> InitializerErrors;

                Analyzer& analyzer;
                std::unordered_set<Return*> returns;
                std::vector<std::shared_ptr<Expression>> parameters;

                Location context;

                // You can only be exported as constructors of one, or nonstatic member of one, class.
                std::unique_ptr<Scope> root_scope;
                const Parse::FunctionBase* fun;
                std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*, Lexer::Range>> clang_exports;
                virtual void ComputeBody();
            public:
                std::shared_ptr<Expression> GetParameter(unsigned num) { return parameters[num]; }
                static void AddDefaultHandlers(Analyzer& a);
                FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Location);

                Type* GetNonstaticMemberContext();

                std::shared_ptr<Expression> LookupLocal(Parse::Name name);
                ~FunctionSkeleton();
                Location GetContext() { return context; }
                const Parse::FunctionBase* GetASTFunction() { return fun; }
                Scope* EnsureComputedBody();
                Type* GetExplicitReturn();
                std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*, Lexer::Range>>& GetClangExports();
            };
        }
    }
}