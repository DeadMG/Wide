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
            const std::vector<Type*>& GetMembers() override final;
            bool HasDeclaredDynamicFunctions() override final { return false; }

            Type* nonstatic;
        public:
            OverloadSet(OverloadSet* s, OverloadSet* other, Analyzer& a, Type* context = nullptr);
            OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* nonstatic, Analyzer& a);
            OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangTU* tu, Type* context, Analyzer& a);

            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            //std::unique_ptr<NotExpression> BuildCall(std::unique_ptr<NotExpression> val, std::vector<std::unique_ptr<NotExpression>> args, Context c);
            Callable* Resolve(std::vector<Type*> types, Type* source);
            Callable* ResolveMember(std::vector<Type*> types,  Type* source) {
                if (nonstatic)
                    types.insert(types.begin(), nonstatic);
                return Resolve(types, source);
            }

            std::string explain() override final;
            Type* GetConstantContext() override final;

            void IssueResolutionError(std::vector<Type*> types, Context c);
            std::pair<ClangTU*, clang::FunctionDecl*> GetSingleFunction();
        };
    }
}