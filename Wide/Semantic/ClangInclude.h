#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class ClangIncludeEntity : public MetaType {
        public:
            ClangIncludeEntity() {}
                
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Analyzer& a, Lexer::Range where) override;
            Expression BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
        };
    }
}