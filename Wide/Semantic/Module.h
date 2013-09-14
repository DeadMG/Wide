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
            AST::Module* m;
            std::unordered_map<std::string, ConcreteExpression> SpecialMembers;
        public:
            using Type::BuildValueConstruction;

            Module(AST::Module* p);
            AST::DeclContext* GetDeclContext() override;
            void AddSpecialMember(std::string name, ConcreteExpression t);            
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Analyzer& a) override;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, Wide::Lexer::TokenType, Analyzer& a) override;
        };
    }
}