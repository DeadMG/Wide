#pragma once

#include <Wide/Semantic/Type.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

#ifndef _MSC_VER
#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)
#endif

namespace Wide {
	namespace AST {
		struct Type;
		struct Expression;
	}
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
                AST::Expression* InClassInitializer;
            };

            // Actually an ordered list of all members
            std::vector<member> llvmtypes;

            // Actually a list of member variables
            std::unordered_map<std::string, unsigned> members;
            std::function<llvm::Type*(llvm::Module*)> ty;
            std::string llvmname;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            std::vector<std::function<llvm::Type*(llvm::Module*)>> types;
            unsigned AdjustFieldOffset(unsigned);
        public:
            std::vector<member> GetMembers() { return llvmtypes; }
            UserDefinedType(AST::Type* t, Analyzer& a, Type* context);
            AST::DeclContext* GetDeclContext() override;
            bool HasMember(std::string name);

            bool IsComplexType() override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;  
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) override;
            Wide::Util::optional<Expression> AccessMember(Expression, std::string name, Analyzer& a) override;
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) override;
            ConversionRank RankConversionFrom(Type* from, Analyzer& a) override;
            Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
			Expression BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType ty, Analyzer& a) override;
        };
    }
}