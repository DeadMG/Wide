#pragma once

#include "Type.h"
#include "../Parser/AST.h"

#ifndef _MSC_VER
#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)
#endif

namespace Wide {
    namespace Semantic {
        class Function;
        class UserDefinedType : public Type {
            Analyzer& a;
            std::size_t align;
            std::size_t allocsize;
            AST::Type* type;
            bool processedconstructors;
            bool iscomplex;
            struct member {
                Type* t;
                // ONLY LLVM Field index, NOT index into llvmtypes
                unsigned num;
                std::string name;
            };

            // Actually an ordered list of all members
            std::vector<member> llvmtypes;

            // Actually a list of member variables
            std::unordered_map<std::string, unsigned> members;
            std::function<llvm::Type*(llvm::Module*)> ty;
            std::string llvmname;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            std::vector<std::function<llvm::Type*(llvm::Module*)>> types;
            Expression BuildBinaryOperator(std::string opname, Expression lhs, Expression rhs, Analyzer& a);
            unsigned AdjustFieldOffset(unsigned);
        public:
            std::vector<member> GetMembers() { return llvmtypes; }
            UserDefinedType(AST::Type* t, Analyzer& a, Type* context);
            AST::DeclContext* GetDeclContext();
            bool HasMember(std::string name);

            bool IsComplexType();
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);            
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression AccessMember(Expression, std::string name, Analyzer& a);
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
            ConversionRank RankConversionFrom(Type* from, Analyzer& a);
            Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a);
            Expression BuildOr(Expression lhs, Expression rhs, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        };
    }
}