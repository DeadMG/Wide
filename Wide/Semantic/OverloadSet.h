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
    namespace Semantic {
        class Function;
        class UserDefinedType;
        class OverloadSet : public Type {
            std::unordered_set<OverloadResolvable*> callables;
            std::unordered_set<clang::NamedDecl*> clangfuncs;
            ClangUtil::ClangTU* from;

            Type* nonstatic;
        public:
            OverloadSet(OverloadSet* s, OverloadSet* other);
            OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* nonstatic);
            OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangUtil::ClangTU* tu, Type* context);

            OverloadSet* CreateConstructorOverloadSet(Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override final;
            Callable* Resolve(std::vector<Type*> types, Analyzer& a);
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;

            Type* GetConstantContext(Analyzer& a) override final;
        };
    }
}