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
            Type* ReturnType;
            std::vector<Type*> Args;
            Analyzer& analyzer;
            AST::Function* fun;
            Codegen::Function* codefun;
            UserDefinedType* member;
            void ComputeBody(Analyzer& a);

            std::vector<Codegen::Statement*> exprs;
            std::vector<std::unordered_map<std::string, Expression>> variables;
            std::string name;
        public:
            bool HasLocalVariable(std::string name);
            Function(std::vector<Type*> args, AST::Function* astfun, Analyzer& a, UserDefinedType* member = nullptr);        

            clang::QualType GetClangType(ClangUtil::ClangTU& where, Analyzer& a) override;        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override; 
            AST::DeclContext* GetDeclContext() override;
     
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a) override;        
            Wide::Util::optional<Expression> AccessMember(Expression, std::string name, Analyzer& a) override;   
            std::string GetName();
            UserDefinedType* IsMember() { return member; }

            FunctionType* GetSignature(Analyzer& a);
        };
    }
}