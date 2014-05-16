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
            std::unordered_map<std::string, std::unique_ptr<Expression>> SpecialMembers;
        public:
            Module(const AST::Module* p, Module* higher, Analyzer& a);
            Module* GetContext() override final { return context; }
            void AddSpecialMember(std::string name, std::unique_ptr<Expression> t);

            using Type::AccessMember;

            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> val, std::string name, Context c) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* t, Wide::Lexer::TokenType, Lexer::Access access) override final;
            const AST::Module* GetASTModule() { return m; }
            std::string explain() override final;
        };
    }
}