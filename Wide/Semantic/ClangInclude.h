#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
            std::unique_ptr<OverloadResolvable> MacroHandler;
            std::unique_ptr<OverloadResolvable> LiteralHandler;
            std::unique_ptr<OverloadResolvable> HeaderIncluder;
        public:
            ClangIncludeEntity(Analyzer& a) : MetaType(a) {}
            std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            std::string explain() override final;
        };
    }
}