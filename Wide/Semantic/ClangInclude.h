#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
            OverloadSet* MacroOverloadSet = {};
            std::unique_ptr<OverloadResolvable> MacroHandler;
            OverloadSet* LiteralOverloadSet = {};
            std::unique_ptr<OverloadResolvable> LiteralHandler;
            OverloadSet* HeaderOverloadSet = {};
            std::unique_ptr<OverloadResolvable> HeaderIncluder;
        public:
            ClangIncludeEntity(Analyzer& a) : MetaType(a) {}
            std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::string explain() override final;
        };
    }
}