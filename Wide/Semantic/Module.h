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
            std::string name;
            std::unordered_map<std::string, std::shared_ptr<Expression>> SpecialMembers;
        public:
            Module(const Parse::Module* p, Module* higher, std::string name, Analyzer& a);
            Module* GetContext() override final { return context; }
            void AddSpecialMember(std::string name, std::shared_ptr<Expression> t);

            bool IsLookupContext() override final { return true; }
            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::string name1, Context c) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName, Parse::Access access, OperatorAccess kind) override final;
            const Parse::Module* GetASTModule() { return m; }
            std::string explain() override final;

            static void AddDefaultHandlers(Analyzer& a);
        };
    }
}