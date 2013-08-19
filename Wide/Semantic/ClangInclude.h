#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Wide::Util::optional<Expression> AccessMember(Expression, std::string name, Analyzer& a) override;
            Expression BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) override;
        };
    }
}