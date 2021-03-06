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
        class UserDefinedType;
        class OverloadSet : public AggregateType {
            std::unique_ptr<OverloadResolvable> ReferenceConstructor;
            std::unique_ptr<OverloadResolvable> ResolveResolvable;

            std::unordered_set<OverloadResolvable*> callables;
            std::unordered_set<clang::NamedDecl*> clangfuncs;
            ClangTU* from;

            std::vector<Type*> contents;
            std::vector<Type*> GetMembers() override final;

            Type* nonstatic;
        public:
            OverloadSet(OverloadSet* s, OverloadSet* other, Analyzer& a, Type* nonstatic_override = nullptr);
            OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* nonstatic, Analyzer& a);
            OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangTU* tu, Type* context, Analyzer& a);

            std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            Callable* Resolve(std::vector<Type*> types, Location source); 
            std::pair<Callable*, std::vector<Type*>> ResolveWithArguments(std::vector<Type*> types, Location source);

            std::string explain() override final;
            bool IsNonstatic() { return nonstatic; }
            bool IsConstant() override final;

            std::shared_ptr<Expression> IssueResolutionError(std::vector<Type*> types, Context c);
            std::pair<ClangTU*, clang::FunctionDecl*> GetSingleFunction();
        };
    }
}