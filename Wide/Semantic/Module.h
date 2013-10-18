#pragma once

#include <Wide/Semantic/MetaType.h>
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
        public:
            Module(const AST::Module* p, Module* higher);
            Module* GetContext(Analyzer& a) override { return context; }
            void AddSpecialMember(std::string name, ConcreteExpression t);          

            using Type::AccessMember;

            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Analyzer& a, Lexer::Range where) override;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, Wide::Lexer::TokenType, Analyzer& a, Lexer::Range where) override;
        };
    }
}