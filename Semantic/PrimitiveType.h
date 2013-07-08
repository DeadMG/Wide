#pragma once

#include <Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        // Used for default implementations of assignment, etc.
        class PrimitiveType : public Type {
        public:
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a);
        };
    }
}