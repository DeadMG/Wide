#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
            OverloadSet* MangleOverloadSet = {};
            OverloadSet* MacroOverloadSet = {};
            OverloadSet* LiteralOverloadSet = {};
        public:
                
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override;
            ConcreteExpression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) override;
        };
    }
}