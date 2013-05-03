#pragma once

#include "Type.h"
#include "../Parser/AST.h"

namespace Wide {
    namespace Semantic {
        class UserDefinedType : public Type {
            AST::Type* type;
            bool iscomplex;
            struct member {
                Type* t;
                unsigned num;
                std::function<llvm::Type*(llvm::Module*)> llvmty;
                std::string name;
            };
            std::vector<member> llvmtypes;
            std::unordered_map<std::string, unsigned> members;
            std::function<llvm::Type*(llvm::Module*)> ty;
            std::string llvmname;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
        public:
            UserDefinedType(AST::Type* t, Analyzer& a);

            bool IsComplexType();
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression AccessMember(Expression, std::string name, Analyzer& a);
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
        };
    }
}