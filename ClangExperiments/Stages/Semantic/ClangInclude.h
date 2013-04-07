#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public Type {
            Analyzer& a;
        public:
            ClangIncludeEntity(Analyzer& anal)
                : a(anal) {}
                
            Expression AccessMember(Expression, std::string name, Analyzer& a);
            Expression BuildCall(Expression e, std::vector<Expression> args, Analyzer& a);
        };
    }
}