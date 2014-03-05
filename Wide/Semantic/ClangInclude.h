#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
            OverloadSet* MangleOverloadSet = {};
            OverloadSet* MacroOverloadSet = {};
            OverloadSet* LiteralOverloadSet = {};
            OverloadSet* HeaderOverloadSet = {};
        public:
                
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override final;
            ConcreteExpression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}