#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Codegen/Expression.h>
#include <vector>
#include <unordered_map>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

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
        class Function : public Type {
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
            UserDefinedType* member;
            ReturnState returnstate;
            void ComputeBody(Analyzer& a);

            void CompleteAnalysis(Type* ret, Analyzer& a);

            std::vector<Codegen::Statement*> exprs;
            std::vector<std::unordered_map<std::string, ConcreteExpression>> variables;
            std::string name;
        public:
            bool HasLocalVariable(std::string name);
            Function(std::vector<Type*> args, const AST::Function* astfun, Analyzer& a, UserDefinedType* member = nullptr);        

            clang::QualType GetClangType(ClangUtil::ClangTU& where, Analyzer& a) override;        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override; 
            const AST::DeclContext* GetDeclContext() override;
     
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Analyzer& a) override;
            std::string GetName();
            UserDefinedType* IsMember() { return member; }

            FunctionType* GetSignature(Analyzer& a);
        };
    }
}