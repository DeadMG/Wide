#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Codegen/Expression.h>
#include <vector>
#include <unordered_map>

namespace llvm {
    class Function;
}
namespace Wide {
    namespace AST {
        struct Function;
    }
    namespace Codegen {
        class Function;
    }
    namespace Semantic {
        class FunctionType;
        class UserDefinedType;
        class Function : public Callable, public MetaType {
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            enum ReturnState {
                NoReturnSeen,
                DeferredReturnSeen,
                ConcreteReturnSeen
            };
            State s;
            Type* ReturnType;
            std::vector<Type*> Args;
            Analyzer& analyzer;
            const AST::Function* fun;
            Codegen::Function* codefun;
            ReturnState returnstate;
            void ComputeBody(Analyzer& a);
            Type* context;

            void CompleteAnalysis(Type* ret, Analyzer& a);

            std::vector<Codegen::Statement*> exprs;
            struct Scope {
                Scope(Scope* s) : parent(s), current_while(nullptr) {}
                Scope* parent;
                std::vector<std::unique_ptr<Scope>> children;
                std::unordered_map<std::string, ConcreteExpression> named_variables;
                std::vector<ConcreteExpression> needs_destruction;
                Codegen::WhileStatement* current_while;

                Codegen::Statement* GetCleanupExpression(Analyzer& a, Lexer::Range where);
                Codegen::Statement* GetCleanupAllExpression(Analyzer& a, Lexer::Range where);

                Wide::Util::optional<ConcreteExpression> LookupName(std::string name);
            };
            struct LocalScope;
            Scope root_scope;
            Scope* current_scope;
            std::string name;
        public:
            bool HasLocalVariable(std::string name);
            Function(std::vector<Type*> args, const AST::Function* astfun, Analyzer& a, Type* container);        

            clang::QualType GetClangType(ClangUtil::ClangTU& where, Analyzer& a) override final;
     
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override final;
            std::string GetName();
            Type* GetContext(Analyzer& a) override final { return context; }

            FunctionType* GetSignature(Analyzer& a);
            std::vector<Type*> GetArgumentTypes(Analyzer& a) override final {
                return Args;
            }
            bool AddThis() override final;
            Wide::Util::optional<ConcreteExpression> LookupLocal(std::string name, Context c);
            Type* GetConstantContext(Analyzer& a) override final;
        };
    }
}