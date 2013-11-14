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
            std::unordered_map<std::string, Expression> SpecialMembers;
        public:
            Module(const AST::Module* p, Module* higher);
            Module* GetContext(Analyzer& a) override { return context; }
            void AddSpecialMember(std::string name, Expression t);          

            using Type::AccessMember;

            Wide::Util::optional<Expression> AccessMember(ConcreteExpression val, std::string name, Context c) override;
            OverloadSet* AccessMember(ConcreteExpression val, Wide::Lexer::TokenType, Context c) override;
        };
    }
}