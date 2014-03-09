#pragma once

#include <Wide/Semantic/Type.h>
#include <unordered_map>
#include <string>

namespace Wide {
    namespace AST {
        struct Module;
    }
    namespace Semantic {
        class Module : public MetaType {
            const AST::Module* m;
            Module* context;
            std::unordered_map<std::string, ConcreteExpression> SpecialMembers;
            std::unordered_map<std::string, ConcreteExpression> CachedLookups;
        public:
            Module(const AST::Module* p, Module* higher);
            Module* GetContext(Analyzer& a) override { return context; }
            void AddSpecialMember(std::string name, ConcreteExpression t);

            using Type::AccessMember;

            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Context c) override;
            OverloadSet* CreateOperatorOverloadSet(Type* t, Wide::Lexer::TokenType, Lexer::Access access, Analyzer& a) override;
            const AST::Module* GetASTModule() { return m; }
            std::string explain(Analyzer& a) override final;
        };
    }
}