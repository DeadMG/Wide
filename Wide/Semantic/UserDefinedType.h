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
           
            Type* GetContext(Analyzer& a) override final { return context; }

            bool HasMember(std::string name);

            bool IsComplexType() override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;

            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override final;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override final;
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Context c) override final;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            bool IsCopyable(Analyzer& a) override final;
            bool IsMovable(Analyzer& a) override final;
        };
    }
}