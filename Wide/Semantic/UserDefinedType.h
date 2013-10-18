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
        struct Function;
    }
    namespace Semantic {
        class Function;
        class OverloadSet;
        class Module;
        class UserDefinedType : public Type {
            std::size_t align;
            std::size_t allocsize;
            const AST::Type* type;
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
            OverloadSet* destructor;
            OverloadSet* constructor;

            // Actually a list of member variables
            std::unordered_map<std::string, unsigned> members;
            std::function<llvm::Type*(llvm::Module*)> ty;
            std::string llvmname;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            std::vector<std::function<llvm::Type*(llvm::Module*)>> types;
            Type* context;

            unsigned AdjustFieldOffset(unsigned);
            AST::Function* AddCopyConstructor(Analyzer& a);
            AST::Function* AddMoveConstructor(Analyzer& a);
            AST::Function* AddDefaultConstructor(Analyzer& a);
        public:
            std::vector<member> GetMembers() { return llvmtypes; }
            UserDefinedType(const AST::Type* t, Analyzer& a, Type* context);
           
            Type* GetContext(Analyzer& a) override { return context; }

            bool HasMember(std::string name);

            bool IsComplexType() override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;  
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Analyzer& a, Lexer::Range where) override;
            ConversionRank RankConversionFrom(Type* from, Analyzer& a) override;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType ty, Analyzer& a, Lexer::Range where) override;
        };
    }
}