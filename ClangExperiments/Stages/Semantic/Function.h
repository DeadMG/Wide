#pragma once

#include "Type.h"
#include "../Codegen/Expression.h"

#define _SCL_SECURE_NO_WARNINGS

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
        class Function : public Type {
            Type* ReturnType;
            std::vector<Type*> Args;
            Analyzer& analyzer;
            AST::Function* fun;
            Codegen::Function* codefun;
            bool body;
            Type* member;
            void ComputeBody(Analyzer& a);

            std::vector<Codegen::Statement*> exprs;
            std::vector<std::unordered_map<std::string, Expression>> variables;
            std::string name;
        public:
            Function(std::vector<Type*> args, AST::Function* astfun, Analyzer& a, Type* member = nullptr);        

            clang::QualType GetClangType(ClangUtil::ClangTU& where, Analyzer& a);        
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);      
     
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a);        
            Expression AccessMember(Expression, std::string name, Analyzer& a);   
            std::string GetName();

            FunctionType* GetSignature(Analyzer& a);
        };
    }
}