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
            Location context;
            std::string name;
            std::unordered_map<std::string, std::shared_ptr<Expression>> SpecialMembers;
        public:
            Module(const Parse::Module* p, Location location, std::string name, Analyzer& a);
            void AddSpecialMember(std::string name, std::shared_ptr<Expression> t);

            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::string name1, Context c) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName, Parse::Access access, OperatorAccess kind) override final;
            const Parse::Module* GetASTModule() { return m; }
            std::string explain() override final;
            bool IsLookupContext() override final;

            static void AddDefaultHandlers(Analyzer& a);
        };
    }
}