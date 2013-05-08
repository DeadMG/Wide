#pragma once

#include "Type.h"

#include <unordered_map>
#include <string>

namespace Wide {
    namespace AST {
        struct Module;
    }
    namespace Semantic {
        class Module : public Type {
            AST::Module* m;
            std::unordered_map<std::string, Expression> SpecialMembers;
        public:
            Module(AST::Module* p);
            AST::DeclContext* GetDeclContext();
            void AddSpecialMember(std::string name, Expression t);            
            Expression AccessMember(Expression val, std::string name, Analyzer& a);
        };
    }
}