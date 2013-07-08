#pragma once

#include <Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Expression AccessMember(Expression, std::string name, Analyzer& a);
            Expression BuildCall(Expression e, std::vector<Expression> args, Analyzer& a);
        };
    }
}