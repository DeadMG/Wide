#pragma once

#include <Wide/Semantic/AggregateType.h>
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
    class FunctionDecl;
}
namespace Wide {
    namespace Semantic {
        class Function;
        class UserDefinedType;
        class OverloadSet : public AggregateType {
            std::unique_ptr<OverloadResolvable> ReferenceConstructor;
            std::unique_ptr<MetaType> ResolveType;

            std::unordered_set<OverloadResolvable*> callables;
            std::unordered_set<clang::NamedDecl*> clangfuncs;
            ClangTU* from;

            std::vector<Type*> contents;
            std::vector<Type*> GetMembers() override final;

            Type* nonstatic;
        public:
            OverloadSet(OverloadSet* s, OverloadSet* other, Analyzer& a, Type* context = nullptr);
            OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* nonstatic, Analyzer& a);
            OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangTU* tu, Type* context, Analyzer& a);

            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            Callable* Resolve(std::vector<Type*> types, Type* source); 
            std::pair<Callable*, std::vector<Type*>> ResolveWithArguments(std::vector<Type*> types, Type* source);

            std::string explain() override final;
            bool IsNonstatic() { return nonstatic; }

            void IssueResolutionError(std::vector<Type*> types, Context c);
            std::pair<ClangTU*, clang::FunctionDecl*> GetSingleFunction();
        };
    }
}