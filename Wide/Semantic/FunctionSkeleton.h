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
            virtual Type* GetReturnType(Expression::InstanceKey key) = 0;
        };
        class FunctionSkeleton {
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };

            std::unordered_set<Return*> returns;
            Type* context;
            std::string source_name;
            std::vector<std::shared_ptr<Expression>> parameters;
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*>> clang_exports;

            // You can only be exported as constructors of one, or nonstatic member of one, class.
            Wide::Util::optional<Semantic::ConstructorContext*> ConstructorContext;
            Wide::Util::optional<Type*> NonstaticMemberContext;
            State current_state;
            std::unique_ptr<Scope> root_scope;
            const Parse::FunctionBase* fun;
        public:
            static void AddDefaultHandlers(Analyzer& a);
            FunctionSkeleton(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::string name, Type* nonstatic_context);

            Type* GetNonstaticMemberContext() { if (NonstaticMemberContext) return *NonstaticMemberContext; return nullptr; }

            std::shared_ptr<Expression> LookupLocal(Parse::Name name);
            std::string GetSourceName() { return source_name; }
            std::shared_ptr<Expression> GetStaticSelf();
            std::string GetExportBody();
            ~FunctionSkeleton(); 

            Type* GetContext();
            const Parse::FunctionBase* GetASTFunction();
            std::vector<Statement*> ComputeBody();
            std::unordered_set<Return*> GetReturns();
            Type* GetExplicitReturn(Expression::InstanceKey key);
        };
    }
}