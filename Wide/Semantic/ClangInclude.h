#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Context c) override;
            Expression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) override;
        };
    }
}