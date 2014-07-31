#pragma once

#include <Wide/Semantic/Type.h>
#include <unordered_map>
#include <string>

namespace Wide {
    namespace Parse {
        struct Module;
    }
    namespace Semantic {
        class Module : public MetaType {
            const Parse::Module* m;
            Module* context;
            std::unordered_map<std::string, std::shared_ptr<Expression>> SpecialMembers;
        public:
            Module(const Parse::Module* p, Module* higher, Analyzer& a);
            Module* GetContext() override final { return context; }
            void AddSpecialMember(std::string name, std::shared_ptr<Expression> t);

            using Type::AccessMember;

            std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> val, std::string name, Context c) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName, Parse::Access access) override final;
            const Parse::Module* GetASTModule() { return m; }
            std::string explain() override final;
        };
    }
}