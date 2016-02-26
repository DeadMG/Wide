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
            boost::signals2::signal<void(Type*)> OnReturnType;
            virtual Type* GetReturnType(Expression::InstanceKey key) = 0;
        };
        class FunctionSkeleton {
            std::unordered_map<const Parse::Expression*, std::unique_ptr<Semantic::Error>> ExportErrors;
            std::unique_ptr<Semantic::Error> ExplicitReturnError;
            std::unordered_map<const Parse::VariableInitializer*, std::unique_ptr<Semantic::Error>> InitializerErrors;

            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            Analyzer& analyzer;
            std::unordered_set<Return*> returns;
            Type* context;
            std::vector<std::shared_ptr<Expression>> parameters;

            // You can only be exported as constructors of one, or nonstatic member of one, class.
            std::function<Type*(Expression::InstanceKey)> NonstaticMemberContext;
            State current_state;
            std::unique_ptr<Scope> root_scope;
            const Parse::FunctionBase* fun;
            std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>)> NonstaticLookup;
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*, Lexer::Range>> clang_exports;

            static std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> BindNonstaticLookup(Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)>);
            std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> BindNonstaticLookup();
        public:
            std::shared_ptr<Expression> GetParameter(unsigned num) { return parameters[num]; }
            static void AddDefaultHandlers(Analyzer& a);
            FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::function<Type*(Expression::InstanceKey)>, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>)> NonstaticLookup);
            FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Type* container, Type*, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>)> NonstaticLookup);
            FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)>)> NonstaticLookup);

            Type* GetNonstaticMemberContext(Expression::InstanceKey key);

            std::shared_ptr<Expression> LookupLocal(Parse::Name name);
            ~FunctionSkeleton(); 

            Type* GetContext() { return context; }
            const Parse::FunctionBase* GetASTFunction() { return fun; }
            Scope* ComputeBody();
            Type* GetExplicitReturn(Expression::InstanceKey key);
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*, Lexer::Range>>& GetClangExports();
        };
    }
}