#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override;
            ConcreteExpression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) override;
        };
    }
}