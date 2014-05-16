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
            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) override final;
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::string explain() override final;
        };
    }
}