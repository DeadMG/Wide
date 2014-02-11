#pragma once

#include <Wide/Semantic/AggregateType.h>
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
        class UserDefinedType : public AggregateType {
            const AST::Type* type;
            bool IsBinaryComplex;
            bool IsUserDefinedComplex;
            bool HasDefaultConstructor;

            // Actually a list of member variables
            std::unordered_map<std::string, unsigned> members;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            Type* context;
            struct member {
                std::string name;
                Type* t;
                unsigned num;
            };
        public:
            std::vector<member> GetMembers();
            UserDefinedType(const AST::Type* t, Analyzer& a, Type* context);           
            Type* GetContext(Analyzer& a) override final { return context; }
            bool HasMember(std::string name);            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override final;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override final;
            ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override final;

            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType member, Analyzer& a) override final;
            OverloadSet* CreateDestructorOverloadSet(Analyzer& a) override final;

            bool IsCopyConstructible(Analyzer& a) override final;
            bool IsMoveConstructible(Analyzer& a) override final;
            bool IsCopyAssignable(Analyzer& a) override final;
            bool IsMoveAssignable(Analyzer& a) override final;
            bool IsComplexType() override final;

            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple();
        };
    }
}