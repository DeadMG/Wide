#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Analyzer& a) override;
            Expression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Analyzer& a) override;
        };
    }
}