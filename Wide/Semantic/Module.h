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
            std::unordered_map<std::string, Expression> SpecialMembers;
        public:
            using Type::BuildValueConstruction;

            Module(AST::Module* p);
            AST::DeclContext* GetDeclContext() override;
            void AddSpecialMember(std::string name, Expression t);            
            Wide::Util::optional<Expression> AccessMember(Expression val, std::string name, Analyzer& a) override;
            Wide::Util::optional<Expression> AccessMember(Expression val, Wide::Lexer::TokenType, Analyzer& a) override;
        };
    }
}