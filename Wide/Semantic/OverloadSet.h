#pragma once

#include <Wide/Semantic/Type.h>
#include <memory>
#include <unordered_set>
#include <unordered_map>

#ifndef _MSC_VER
#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)
#endif

namespace clang {
    class NamedDecl;
}
namespace Wide {
    namespace AST {
        struct FunctionOverloadSet;
        struct Function;
    }
    namespace Semantic {
        class Function;
        class UserDefinedType;
        class OverloadSet : public Type {
            std::unordered_map<Type*, std::unordered_set<const AST::Function*>> functions;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            std::unordered_set<Callable*> callables;
            std::unordered_set<clang::NamedDecl*> clangfuncs;
            ClangUtil::ClangTU* from;

            Type* nonstatic;
        public:
            OverloadSet(const AST::FunctionOverloadSet* s,Type* context);
            OverloadSet(OverloadSet* s, OverloadSet* other);
            OverloadSet(std::unordered_set<const AST::Function*>, Type* context);
            OverloadSet(std::unordered_set<Callable*> call);
            OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangUtil::ClangTU* tu, Type* context);

            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            Callable* Resolve(std::vector<Type*> types, Analyzer& a);
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;

            using Type::BuildValueConstruction;
        };
    }
}