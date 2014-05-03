#pragma once

#include <Wide/Semantic/Type.h>
#include <vector>
#include <unordered_map>

namespace llvm {
    class Function;
}
namespace Wide {
    namespace AST {
        struct FunctionBase;
        struct Statement;
    }
    namespace Codegen {
        class Function;
    }
    namespace Semantic {
        class FunctionType;
        class UserDefinedType;
        class Function : public MetaType, public Callable {
            // Implementation detail helpers
            struct ReturnStatement;
            struct CompoundStatement;
            struct WhileStatement;
            struct VariableStatement;
            struct Scope;
            struct LocalScope;
            struct LocalVariable;
            struct IfStatement;
            struct VariableReference;
            struct ConditionVariable;
            struct BreakStatement;
            struct ContinueStatement;
            struct InitVTable;
            struct InitMember;
            struct Parameter;
            llvm::Function* llvmfunc = nullptr;
            std::unique_ptr<Statement> AnalyzeStatement(const AST::Statement*);
            std::unordered_set<ReturnStatement*> returns;
            std::vector<std::unique_ptr<Statement>> stmts;
            std::unique_ptr<Scope> root_scope;
            Scope* current_scope;
            Wide::Util::optional<Type*> ExplicitReturnType;
            Type* ReturnType = nullptr;
            Analyzer& analyzer;
            std::vector<Type*> Args;
            const AST::FunctionBase* fun;
            Type* context;
            std::string source_name;
            std::string name;
            Wide::Util::optional<std::string> trampoline;
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            State s;
            
            /*
            Codegen::Function* codefun;
            void CompleteAnalysis(Analyzer& a);

            std::vector<std::unique_ptr<Statement>> exprs;
            struct Scope {
                Scope(Scope* s) : parent(s) {}
                Scope* parent;
                std::vector<std::unique_ptr<Scope>> children;
                std::unordered_map<std::string, std::pair<ConcreteExpression, Lexer::Range>> named_variables;
                std::vector<ConcreteExpression> needs_destruction;

                Wide::Util::optional<std::pair<ConcreteExpression, Lexer::Range>> LookupName(std::string name, Context c);
            };
            struct LocalScope;
            Scope root_scope;
            Scope* current_scope;*/
            void ComputeReturnType();

            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final {
                return AdjustArgumentsForTypes(args, Args, c);
            }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> exprs, Context c) override final {
                return BuildCall(BuildValueConstruction({}, c), std::move(exprs), c);
            }
        public:
            void ComputeBody();
            void EmitCode(Codegen::Generator& g);
            Function(std::vector<Type*> args, const AST::FunctionBase* astfun, Analyzer& a, Type* container, std::string name);        

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& where) override final;
     
            std::unique_ptr<Expression> BuildCall(Expression* val, std::vector<Expression*> args) override final;
            std::string GetName();
            Type* GetContext() override final { return context; }

            FunctionType* GetSignature();
            Wide::Util::optional<ConcreteExpression> LookupLocal(std::string name);
            Type* GetConstantContext() override final;
            void SetExportName(std::string name) { trampoline = name; }
            std::string explain() override final;
            std::string GetSourceName() { return source_name; }
            ~Function();
        };
    }
}